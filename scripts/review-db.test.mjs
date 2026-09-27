import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    detectEol,
    loadDatabase,
    parseJson,
    parseReviewsTxt,
    parseDatabase,
    serializeDatabase,
    sameReview,
    sortReviews,
} from './review-db.mjs';

import { checkSubmission, parseSubmission } from './apply-submission.mjs';

test('a trailing comma is reported with a line and a caret', () => {
    // This is the mistake that cost an afternoon, so the message has to be
    // better than "position 8"
    const inArray = ['{', '  "reviews": [', '    { "levelId": 1, "text": "x" },', '  ]', '}'].join('\n');
    const inObject = ['{', '  "a": 1,', '}'].join('\n');

    for (const [text, line] of [[inArray, '3'], [inObject, '2']]) {
        assert.throws(
            () => parseJson(text, 'reviews.json'),
            err => {
                assert.match(err.message, /a comma right before/, `message was: ${err.message}`);
                assert.match(err.message, new RegExp(`line ${line}`));
                assert.match(err.message, /\^/);
                return true;
            }
        );
    }
});

test('the exact mistake from reviews.json is called out', () => {
    const broken = [
        '{',
        '  "version": 2,',
        '  "reviews": [',
        '    { "levelId": 1, "text": "one" },',
        '    { "levelId": 2, "text": "two" },',
        '  ]',
        '}',
    ].join('\n');

    assert.throws(() => parseJson(broken, 'reviews.json'), err => {
        assert.match(err.message, /a comma right before "\]"/);
        assert.match(err.message, /line 5/);
        return true;
    });
});

test('other JSON mistakes are located too', () => {
    const cases = [
        ['{\n  "a" 1\n}', /colon is missing/],
        ['{\n  a: 1\n}', /double quotes/],
        ['{\n  "a": 1\n  "b": 2\n}', /comma/],
        ['{\n  "a": "unclosed\n}', /never closed/],
    ];

    for (const [text, pattern] of cases) {
        assert.throws(() => parseJson(text, 'reviews.json'), pattern, `should report ${text}`);
    }
});

test('a valid document is not rejected by the scanner', () => {
    const fine = '{\n  "version": 2,\n  "reviews": [\n    { "levelId": 1, "text": "a, b {c}" }\n  ]\n}\n';
    assert.doesNotThrow(() => parseJson(fine, 'reviews.json'));
});

test('the legacy map format is still read', () => {
    const problems = [];
    const reviews = loadDatabase(
        JSON.stringify({
            1234567: { author: 'Starr', text: 'good level' },
            7654321: 'no author here',
        }),
        problems
    );

    assert.deepEqual(problems, []);
    assert.equal(reviews.length, 2);
    assert.equal(reviews[0].levelId, 1234567);
    assert.equal(reviews[0].author, 'Starr');
    assert.equal(reviews[1].levelId, 7654321);
    assert.equal(reviews[1].author, '');
});

test('a level can have several reviews', () => {
    const reviews = parseDatabase({
        reviews: [
            { levelId: 1, author: 'a', text: 'first', date: '2026-01-01T00:00:00Z' },
            { levelId: 1, author: 'b', text: 'second', date: '2026-02-01T00:00:00Z' },
            { levelId: 2, author: 'c', text: 'other level', date: '2026-03-01T00:00:00Z' },
        ],
    });

    assert.equal(reviews.length, 3);
    assert.equal(reviews.filter(review => review.levelId === 1).length, 2);
});

test('one broken entry does not take the rest of the database down', () => {
    const problems = [];
    const reviews = loadDatabase(
        JSON.stringify({
            reviews: [
                { levelId: 1, author: 'a', text: 'fine' },
                { author: 'b', text: 'no level id' },
                { levelId: 3, text: '   ' },
                { levelId: 4, author: 'c', text: 'also fine' },
                'not an object at all',
            ],
        }),
        problems
    );

    assert.equal(reviews.length, 2);
    assert.equal(problems.length, 3);
    assert.match(problems[0], /reviews\[1\]: missing or invalid levelId/);
    assert.match(problems[1], /reviews\[2\]: text is empty/);
});

test('the same review twice is stored once', () => {
    const problems = [];
    const reviews = loadDatabase(
        JSON.stringify({
            reviews: [
                { levelId: 1, author: 'a', text: 'same' },
                { levelId: 1, author: 'a', text: 'same' },
            ],
        }),
        problems
    );

    assert.equal(reviews.length, 1);
    assert.match(problems[0], /already in the database/);
});

test('a document that is not a database is refused outright', () => {
    const problems = [];
    assert.deepEqual(parseDatabase('nonsense', problems), []);
    assert.match(problems[0], /neither an object nor an array/);
});

test('newest first, undated last', () => {
    const sorted = sortReviews([
        { date: '', author: 'undated' },
        { date: '2026-01-01T00:00:00Z', author: 'older' },
        { date: '2026-06-01T00:00:00Z', author: 'newer' },
    ]);

    assert.deepEqual(sorted.map(review => review.author), ['newer', 'older', 'undated']);
});

test('an unknown date is not invented', () => {
    const reviews = loadDatabase(JSON.stringify({ reviews: [{ levelId: 1, text: 'x' }] }));
    assert.equal(reviews[0].date, '');
});

test('the database round trips', () => {
    const original = [
        { id: 'a', levelId: 5, levelName: 'Level', author: 'me', text: 'good', date: '2026-05-05T05:05:05Z' },
    ];

    const text = serializeDatabase(original, '\n');
    assert.deepEqual(loadDatabase(text), original);
});

test('line endings survive a rewrite', () => {
    assert.equal(detectEol('a\r\nb'), '\r\n');
    assert.equal(detectEol('a\nb'), '\n');
    assert.ok(serializeDatabase([], '\r\n').includes('\r\n'));
});

test('reviews.txt reads with and without an author', () => {
    const problems = [];
    const reviews = parseReviewsTxt(
        [
            '# a comment',
            '',
            '1234567|Starr|has an author',
            '7654321|only text',
            'not a level||missing id',
            '5555555|someone|',
        ].join('\n'),
        problems
    );

    assert.equal(reviews.length, 2);
    assert.equal(reviews[0].author, 'Starr');
    assert.equal(reviews[1].author, '');
    assert.equal(reviews[1].text, 'only text');
    assert.equal(problems.length, 2);
    assert.match(problems[0], /bad level id/);
    assert.match(problems[1], /empty review/);
});

test('a submission written by the mod is understood', () => {
    const submission = parseSubmission(
        [
            '### Review submission',
            '',
            'Level: Stereo Madness',
            'Level ID: 1234567',
            'Author: NormanRus',
            '',
            'Write the review under the line below.',
            '',
            '---',
            '',
            'Music fits perfectly and the pattern is fair.',
        ].join('\n')
    );

    assert.deepEqual(submission, {
        levelId: 1234567,
        levelName: 'Stereo Madness',
        author: 'NormanRus',
        text: 'Music fits perfectly and the pattern is fair.',
    });
    assert.deepEqual(checkSubmission(submission), []);
});

test('a submission that was never filled in is refused', () => {
    const submission = parseSubmission(
        [
            'Level: Stereo Madness',
            'Level ID: 1234567',
            'Author: NormanRus',
            '',
            'Write the review under the line below.',
            '',
            '---',
        ].join('\n')
    );

    assert.match(checkSubmission(submission)[0], /empty/);
});

test('the template text left in place is refused', () => {
    const submission = parseSubmission(
        ['Level ID: 1', 'Author: a', '---', 'Write the review under the line below.'].join('\n')
    );

    assert.match(checkSubmission(submission)[0], /template text/);
});

test('an anonymous submission falls back to the issue author', () => {
    const submission = parseSubmission(['Level ID: 1', 'Author: Anonymous', '---', 'A real review here.'].join('\n'), 'octocat');

    assert.equal(submission.author, 'octocat');
    assert.deepEqual(checkSubmission(submission), []);
});

test('sameReview compares what a reader sees', () => {
    const a = { levelId: 1, author: 'x', text: 'y' };
    assert.ok(sameReview(a, { levelId: 1, author: 'x', text: 'y', id: 'other' }));
    assert.ok(!sameReview(a, { levelId: 1, author: 'x', text: 'z' }));
});
