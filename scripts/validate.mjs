#!/usr/bin/env node
// Checks the database before it is published. Run by CI and usable locally:
//
//   node scripts/validate.mjs
//
// Exits non-zero when the database cannot be published as it is.

import { readFileSync } from 'node:fs';

import { loadDatabase, parseReviewsTxt, sameReview, sortReviews } from './review-db.mjs';

const root = new URL('../', import.meta.url);

function read(path) {
    try {
        return readFileSync(path, 'utf8');
    } catch (err) {
        throw new Error(`cannot read ${path}: ${err.message}`);
    }
}

const errors = [];
const warnings = [];

let reviews = [];
try {
    const text = read(new URL('docs/reviews.json', root));
    const problems = [];
    reviews = loadDatabase(text, problems);
    problems.forEach(problem => warnings.push(`reviews.json: ${problem}`));
} catch (err) {
    errors.push(err.message);
}

let local = [];
try {
    const problems = [];
    local = parseReviewsTxt(read(new URL('reviews.txt', root)), problems);
    problems.forEach(problem => warnings.push(`reviews.txt: ${problem}`));
} catch (err) {
    errors.push(err.message);
}

if (!errors.length) {
    // The same level in both files used to look like two different levels because
    // the ids drifted apart, so any overlap is worth pointing at.
    for (const entry of local) {
        const inDatabase = reviews.some(review => review.levelId === entry.levelId);
        if (inDatabase && !reviews.some(review => sameReview(review, entry))) {
            warnings.push(
                `reviews.txt: level ${entry.levelId} also has a different review in reviews.json, ` +
                'the file only applies offline and on top of a fresh download'
            );
        }
    }

    const ids = new Map();
    for (const review of reviews) {
        ids.set(review.levelId, (ids.get(review.levelId) ?? 0) + 1);
    }
    for (const [levelId, count] of ids) {
        if (count > 1) console.log(`level ${levelId} has ${count} reviews`);
    }

    const undated = reviews.filter(review => !review.date).length;
    if (undated) warnings.push(`reviews.json: ${undated} reviews have no date and will be shown last`);
}

if (warnings.length) {
    console.log(`\n${warnings.length} warning(s):`);
    for (const warning of warnings) console.log(`  - ${warning}`);
}

if (errors.length) {
    console.error(`\n${errors.length} error(s):`);
    for (const error of errors) console.error(`  - ${error}`);
    process.exit(1);
}

const total = reviews.length;
const levels = new Set(reviews.map(review => review.levelId)).size;
console.log(`\nreviews.json: ${total} reviews across ${levels} levels`);
console.log(`reviews.txt:  ${local.length} hand written reviews`);
console.log('\nOK');
