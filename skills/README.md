# libxo skills

Instructions for LLM coding agents working with libxo.  Each
subdirectory is one skill: a SKILL.md with the procedure, plus the
reference files, examples, and scripts it points to.  They are plain
Markdown and POSIX shell, and do not depend on any one agent.

    libxo-convert/    convert a C program's output to libxo

These are installed in ${prefix}/share/skills/libxo/ (for example
/usr/local/share/skills/libxo/).

## Claude Code

Claude Code loads skills from ~/.claude/skills/ (for all projects) or
.claude/skills/ in a project.  Link the installed skill there:

    mkdir -p ~/.claude/skills
    ln -s /usr/local/share/skills/libxo/libxo-convert ~/.claude/skills/

The skill is then used when a request matches its description ("convert
this program to libxo"), or can be named explicitly.

## Other agents

Agents that read an AGENTS.md (or a similar instructions file) in the
project can be pointed at the skill.  Add to the project's AGENTS.md:

    ## libxo conversion

    To convert this program's output to libxo, follow
    /usr/local/share/skills/libxo/libxo-convert/SKILL.md exactly,
    reading the files it references as it directs.  Run its scripts
    with "sh", from that directory's scripts/ subdirectory.  Stop
    after phase 2 and wait for review of the data model.

Adjust the path to the install prefix.  Any agent that can read files
and run shell commands can follow the skill.

## Related tools

The skill uses these libxo tools when installed: xolint, xo-lexicon
(and its lexicon in ${prefix}/share/libxo/lexicon/), xocc, and the
fullpath encoder.  It works without xocc and without a lexicon, with
less checking.
