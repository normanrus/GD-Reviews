#!/usr/bin/env node
// Turns an approved review issue into a database entry.
//
// The mod fills in the level and the player, so the only thing a person types
// is the review itself. This script reads that, refuses anything unusable, and
// writes the database back. The workflow commits the result.
//
//   node scripts/apply-submission.mjs --body-file body.txt --author someone
//
// Use --check to validate without writing.

import { randomUUID } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

import { detectEol, loadDatabase, nowISO, serializeDatabase, sameReview, sortReviews, toLevelId } from './review-db.mjs';

const MIN_LENGTH = 10;

// A player who accepted the template without writing anything is the most
// likely mistake, so it is called out by name rather than as an empty review.
const PLACEHOLDER = 'write the review under the line below';

export function parseSubmission(body, fallbackAuthor = '') {
    const lines = String(body ?? '').replace(/\r\n/g, '\n').split('\n');
    const separator = lines.findIndex(line => /^\s*(-{3,}|={3,})\s*$/.test(line));

    const head = separator >= 0 ? lines.slice(0, separator) : lines;
    const fields = {};

    for (const line of head) {
        const match = /^\s*(level id|level|author)\s*:\s*(.*)$/i.exec(line);
        if (match) fields[match[1].toLowerCase()] = match[2].trim();
    }

    const text = (separator >= 0 ? lines.slice(separator + 1).join('\n') : '').trim();
    const author = fields.author && fields.author.toLowerCase() !== 'anonymous'
        ? fields.author
        : fallbackAuthor;

    return {
        levelId: toLevelId(fields['level id']),
        levelName: fields.level ?? '',
        author: (author || '').trim(),
        text,
    };
}

export function checkSubmission(submission) {
    const problems = [];

    if (!submission.levelId) problems.push('no usable level id');
    if (!submission.author) problems.push('no author');
    if (!submission.text) problems.push('the review is empty');
    else if (submission.text.toLowerCase().includes(PLACEHOLDER)) {
        problems.push('the review still contains the template text');
    } else if (submission.text.length < MIN_LENGTH) {
        problems.push(`the review is only ${submission.text.length} characters`);
    }

    return problems;
}

function readArg(name) {
    const index = process.argv.indexOf(name);
    return index >= 0 ? process.argv[index + 1] : undefined;
}

const bodyFile = readArg('--body-file');
const fallbackAuthor = readArg('--author') ?? '';
const checkOnly = process.argv.includes('--check');

function main() {
if (!bodyFile) {
    console.error('usage: node scripts/apply-submission.mjs --body-file <path> [--author <login>] [--check]');
    process.exit(1);
}

const path = new URL('../docs/reviews.json', import.meta.url);
const source = readFileSync(path, 'utf8');

const problems = [];
const reviews = loadDatabase(source, problems);
if (problems.length) {
    console.error('the database has problems and will not be touched:');
    for (const problem of problems) console.error(`  - ${problem}`);
    process.exit(1);
}

const submission = parseSubmission(readFileSync(bodyFile, 'utf8'), fallbackAuthor);
const submissionProblems = checkSubmission(submission);

if (submissionProblems.length) {
    console.error('this submission cannot be published:');
    for (const problem of submissionProblems) console.error(`  - ${problem}`);
    process.exit(2);
}

if (reviews.some(review => sameReview(review, submission))) {
    console.log('this review is already in the database, nothing to do');
    process.exit(0);
}

const review = {
    id: randomUUID(),
    levelId: submission.levelId,
    levelName: submission.levelName,
    author: submission.author,
    text: submission.text,
    date: nowISO(),
};

const updated = [...reviews, review];
console.log(`adding review for level ${review.levelId} by ${review.author} (${review.text.length} characters)`);
console.log(`database goes from ${reviews.length} to ${sortReviews(updated).length} reviews`);

if (checkOnly) {
    console.log('\nOK (check only, nothing written)');
} else {
    writeFileSync(path, serializeDatabase(updated, detectEol(source)), 'utf8');
    console.log('\nwritten to docs/reviews.json');
}
}

// Keeps the helpers importable from the tests without running the workflow
if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) main();
