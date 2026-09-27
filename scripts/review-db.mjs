// Shared handling of the review database.
//
// The canonical shape is an array, so a level can carry any number of reviews:
//
//   { "version": 2, "reviews": [ { "id", "levelId", "levelName", "author", "text", "date" } ] }
//
// The first database was a map of level id to a single review and is still read,
// so a database that was never migrated keeps working.

import { randomUUID } from 'node:crypto';

export const DB_VERSION = 2;

const DATE = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/;

export function nowISO() {
    return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
}

export function toLevelId(value) {
    if (typeof value === 'number' && Number.isInteger(value) && value > 0) return value;
    if (typeof value === 'string' && /^\d+$/.test(value.trim())) {
        const parsed = Number(value.trim());
        if (Number.isSafeInteger(parsed) && parsed > 0) return parsed;
    }
    return 0;
}

// JSON.parse answers "Unexpected token '}'" for a trailing comma, with no
// position at all, and a single stray comma is exactly what cost an afternoon.
// So the position is found here instead.
function findJsonError(text) {
    let i = 0;

    const bail = (message, at) => {
        throw { index: at ?? i, message };
    };

    const skipSpace = () => {
        while (i < text.length && ' \t\n\r'.includes(text[i])) i++;
    };

    const readString = () => {
        i++;
        while (i < text.length) {
            const c = text[i];
            if (c === '\\') {
                i += 2;
                continue;
            }
            if (c === '"') {
                i++;
                return true;
            }
            if (c === '\n') return false;
            i++;
        }
        return false;
    };

    const readValue = () => {
        skipSpace();
        if (i >= text.length) bail('the file ends in the middle of a value');

        const c = text[i];
        if (c === '"') {
            if (!readString()) bail('a string is never closed');
        } else if (c === '{') {
            readObject();
        } else if (c === '[') {
            readArray();
        } else if (c === '-' || (c >= '0' && c <= '9')) {
            const start = i;
            if (text[i] === '-') i++;
            while (i < text.length && text[i] >= '0' && text[i] <= '9') i++;
            if (text[i] === '.') {
                i++;
                while (i < text.length && text[i] >= '0' && text[i] <= '9') i++;
            }
            if (text[i] === 'e' || text[i] === 'E') {
                i++;
                if (text[i] === '+' || text[i] === '-') i++;
                while (i < text.length && text[i] >= '0' && text[i] <= '9') i++;
            }
            if (i === start) bail('a number is not a number');
        } else if (text.startsWith('true', i)) {
            i += 4;
        } else if (text.startsWith('false', i)) {
            i += 5;
        } else if (text.startsWith('null', i)) {
            i += 4;
        } else {
            bail(`unexpected ${JSON.stringify(c)} where a value was expected`);
        }
    };

    // Shared by objects and arrays: after a value, only a comma or the closing
    // bracket may follow, which is what makes a trailing comma visible.
    const afterValue = (close, what) => {
        skipSpace();
        if (text[i] === ',') {
            // Point at the comma, since that is the character to delete
            const comma = i;
            i++;
            skipSpace();
            if (text[i] === close) {
                bail(`a comma right before ${JSON.stringify(close)}, so the last ${what} has no value`, comma);
            }
            return true;
        }
        if (text[i] === close) return false;
        if (i >= text.length) bail(`the file ends where ${JSON.stringify(close)} was expected`);
        bail(`expected ${JSON.stringify(close)} or a comma but found ${JSON.stringify(text[i])}`);
    };

    function readObject() {
        i++;
        skipSpace();
        if (text[i] === '}') {
            i++;
            return;
        }
        for (;;) {
            skipSpace();
            if (text[i] === '}') bail('a comma right before "}", so the last entry has no value');
            if (text[i] !== '"') {
                if (i >= text.length) bail('the file ends where a key was expected');
                bail(`keys have to be in double quotes, found ${JSON.stringify(text[i])}`);
            }
            if (!readString()) bail('a key is never closed');
            skipSpace();
            if (text[i] !== ':') {
                if (i >= text.length) bail('the file ends where ":" was expected');
                bail(`a colon is missing after the key, found ${JSON.stringify(text[i])}`);
            }
            i++;
            readValue();
            if (!afterValue('}', 'entry')) {
                i++;
                return;
            }
        }
    }

    function readArray() {
        i++;
        skipSpace();
        if (text[i] === ']') {
            i++;
            return;
        }
        for (;;) {
            readValue();
            if (!afterValue(']', 'item')) {
                i++;
                return;
            }
        }
    }

    try {
        readValue();
        skipSpace();
        if (i < text.length) {
            return { index: i, message: 'something follows the database that should not be there' };
        }
        return null;
    } catch (err) {
        return Number.isInteger(err?.index) ? err : null;
    }
}

export function parseJson(text, label) {
    try {
        return JSON.parse(text);
    } catch (err) {
        const found = findJsonError(text);
        if (!found) {
            throw new Error(`${label} is not valid JSON\n  ${err.message}`);
        }

        const lines = text.split('\n');
        let line = 1;
        let seen = 0;
        for (const [number, raw] of lines.entries()) {
            if (found.index <= seen + raw.length) {
                line = number + 1;
                break;
            }
            seen += raw.length + 1;
        }

        const column = found.index - seen + 1;
        const bad = lines[line - 1] ?? '';
        const caret = ' '.repeat(Math.max(0, Math.min(column - 1, bad.length)));

        throw new Error(
            [
                `${label} is not valid JSON`,
                `  ${found.message}`,
                `  line ${line}, column ${column}`,
                `  | ${bad.trim()}`,
                `  | ${caret}^`,
            ].join('\n')
        );
    }
}

export function detectEol(text) {
    return text.includes('\r\n') ? '\r\n' : '\n';
}

function readEntry(entry) {
    if (typeof entry === 'string') return { text: entry };
    return entry && typeof entry === 'object' ? entry : {};
}

// { "1234567": { author, text } } and { "1234567": "text" }
function fromLegacyMap(map) {
    return Object.entries(map).map(([key, value]) => ({ levelId: key, ...readEntry(value) }));
}

function normalize(entry, where, problems) {
    const source = readEntry(entry);
    const levelId = toLevelId(source.levelId ?? source.levelID ?? source.level);
    if (!levelId) {
        problems.push(`${where}: missing or invalid levelId`);
        return null;
    }

    const text = typeof source.text === 'string' ? source.text.trim() : '';
    if (!text) {
        problems.push(`${where}: text is empty`);
        return null;
    }

    const id = typeof source.id === 'string' && source.id.trim() ? source.id.trim() : randomUUID();
    return {
        id,
        levelId,
        levelName: typeof source.levelName === 'string' ? source.levelName.trim() : '',
        author: typeof source.author === 'string' ? source.author.trim() : '',
        text,
        // An invented date would make a hand written review look like it was
        // published today, so unknown stays unknown and simply sorts last.
        date: typeof source.date === 'string' && DATE.test(source.date) ? source.date : '',
    };
}

// Newest first, undated entries last, matching what the mod shows.
export function sortReviews(reviews) {
    return [...reviews].sort((a, b) => {
        const left = a.date ?? '';
        const right = b.date ?? '';
        if (Boolean(left) !== Boolean(right)) return left ? -1 : 1;
        if (left !== right) return left < right ? 1 : -1;
        return String(a.author).localeCompare(String(b.author));
    });
}

export function parseDatabase(raw, problems = []) {
    let entries = null;

    if (Array.isArray(raw)) {
        entries = raw;
    } else if (raw && typeof raw === 'object') {
        if (Array.isArray(raw.reviews)) {
            entries = raw.reviews;
        } else if (raw.reviews && typeof raw.reviews === 'object') {
            entries = fromLegacyMap(raw.reviews);
        } else {
            entries = fromLegacyMap(raw);
        }
    }

    if (!entries) {
        problems.push('database is neither an object nor an array');
        return [];
    }

    const reviews = [];
    const ids = new Set();
    const contents = new Set();

    for (const [index, entry] of entries.entries()) {
        const review = normalize(entry, `reviews[${index}]`, problems);
        if (!review) continue;

        if (ids.has(review.id)) {
            problems.push(`reviews[${index}]: duplicate id ${review.id}`);
            continue;
        }

        const content = `${review.levelId}|${review.author}|${review.text}`;
        if (contents.has(content)) {
            problems.push(`reviews[${index}]: same review is already in the database`);
            continue;
        }

        ids.add(review.id);
        contents.add(content);
        reviews.push(review);
    }

    return reviews;
}

export function loadDatabase(text, problems = []) {
    return parseDatabase(parseJson(text, 'reviews.json'), problems);
}

export function serializeDatabase(reviews, eol = '\n') {
    const ordered = sortReviews(reviews).map(review => ({
        id: review.id,
        levelId: review.levelId,
        levelName: review.levelName ?? '',
        author: review.author ?? '',
        text: review.text,
        date: review.date,
    }));

    return JSON.stringify({ version: DB_VERSION, reviews: ordered }, null, 2).replace(/\n/g, eol) + eol;
}

// "<id>|<author>|<text>" with the author optional. Undated and unnamed, because
// this file is for hand written additions.
export function parseReviewsTxt(text, problems = []) {
    const reviews = [];

    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        if (!line || line.startsWith('#')) continue;

        const first = line.indexOf('|');
        if (first < 0) {
            problems.push(`reviews.txt: no level id in "${line}"`);
            continue;
        }

        const levelId = toLevelId(line.slice(0, first));
        if (!levelId) {
            problems.push(`reviews.txt: bad level id "${line.slice(0, first)}"`);
            continue;
        }

        const rest = line.slice(first + 1);
        const second = rest.indexOf('|');
        const author = second < 0 ? '' : rest.slice(0, second);
        const body = second < 0 ? rest : rest.slice(second + 1);

        if (!body.trim()) {
            problems.push(`reviews.txt: empty review for level ${levelId}`);
            continue;
        }

        reviews.push({ levelId, levelName: '', author: author.trim(), text: body.trim(), id: '', date: '' });
    }

    return reviews;
}

export function sameReview(a, b) {
    return a.levelId === b.levelId && a.author === b.author && a.text === b.text;
}
