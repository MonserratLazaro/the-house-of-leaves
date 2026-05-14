# The House of Leaves — Changing-Word Crossword

A terminal-based crossword puzzle written in C, inspired by *House of Leaves*. The board is alive: unsolved words mutate over time, keeping you on your toes.

## Overview

The House of Leaves presents a six-word crossword (three horizontal, three vertical) where the clues are written in Spanish. The twist: every 20 seconds, one unsolved word silently changes to another entry from its word bank. Solve all six words before the house rewrites itself too many times.

## Gameplay

- The board displays numbered placeholders for unsolved words and revealed letters (shown in green) for solved ones.
- Intersecting cells are highlighted with a yellow `*`.
- Enter the clue number (1–6), followed by your answer, to attempt a word.
- A message appears every 60 seconds to remind you the house is watching.
- Press `CTRL+C` at any time to exit gracefully.

**Horizontal clues:** 1, 2, 3  
**Vertical clues:** 4, 5, 6

## How It Works

The project demonstrates several core systems programming concepts:

| Concept | Usage |
|---|---|
| `fork()` / `waitpid()` | A child process is spawned to render the board; the parent waits for it to finish. |
| POSIX Threads | Two threads run concurrently: one mutates unsolved words on a timer, the other reads user input. |
| `SIGUSR1` | The word-change thread signals the parent to trigger a board redraw. |
| `SIGINT` | Catches `CTRL+C` for a clean exit with a farewell message. |
| `SIGALRM` / `alarm()` | Fires every 60 seconds to display a motivational nudge. |
| `pthread_mutex_t` | A mutex protects all shared state accessed by both threads. |

## Building & Running

**Requirements:** GCC, POSIX-compatible system (Linux/macOS), a terminal with ANSI color support.

```bash
gcc main.c -o house_of_leaves -lpthread
./house_of_leaves
```

## Notes

- Answers are accepted in uppercase or lowercase; accented characters are not required for input.
- If a clue changes while you are typing, finish your input normally. Answers are always evaluated against the most recent version of the clue, even if it changed after you selected it.
- The game ends automatically once all six words are solved.
