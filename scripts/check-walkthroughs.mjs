import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const markdown = readFileSync(resolve(root, 'slides.md'), 'utf8')
const walkthroughs = markdown.split('\nwalkthrough:\n').slice(1)

let checked = 0

for (const [index, walkthrough] of walkthroughs.entries()) {
  const [frontmatter, content = ''] = walkthrough.split('\n---\n', 2)
  const stepCount = (frontmatter.match(/^  - title:/gm) ?? []).length
  const code = content.match(
    /<<< @\/([^\s]+)\s+\w+\s+\{([^}]+)\}\{maxHeight:'(\d+)px'\}/,
  )

  assert(code, `Walkthrough ${index + 1}: must import a full source file with maxHeight`)

  const [, sourcePath, rangesText, maxHeight] = code
  const ranges = rangesText.split('|')

  assert.equal(
    ranges.length,
    stepCount,
    `Walkthrough ${index + 1}: ${ranges.length} code states but ${stepCount} explanations`,
  )
  assert(Number(maxHeight) <= 320, `Walkthrough ${index + 1}: code viewport is too tall to scroll safely`)

  const source = readFileSync(resolve(root, sourcePath), 'utf8')
  const lineCount = source.split('\n').length
  const previouslyHighlighted = new Set()

  for (const [rangeIndex, range] of ranges.entries()) {
    const lines = new Set()
    for (const part of range.split(',')) {
      const [startText, endText = startText] = part.split('-')
      const start = Number(startText)
      const end = Number(endText)
      assert(start >= 1 && end >= start, `Walkthrough ${index + 1}, state ${rangeIndex + 1}: invalid range ${part}`)
      assert(end <= lineCount, `Walkthrough ${index + 1}, state ${rangeIndex + 1}: line ${end} exceeds ${sourcePath}`)
      for (let line = start; line <= end; line += 1)
        lines.add(line)
    }

    for (const line of lines) {
      assert(
        !previouslyHighlighted.has(line),
        `Walkthrough ${index + 1}: line ${line} is highlighted in overlapping concepts`,
      )
      previouslyHighlighted.add(line)
    }
  }

  checked += 1
}

assert.equal(checked, 9, `Expected 9 code walkthrough slides, found ${checked}`)
console.log(`Validated ${checked} synchronized, non-overlapping code walkthroughs.`)
