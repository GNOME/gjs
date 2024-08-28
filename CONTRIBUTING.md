# Contributing to GJS #

## Introduction ##

Thank you for considering contributing to GJS!
As with any open source project, we can't make it as good as possible
without help from you and others.

We do have some guidelines for contributing, set out in this file.
Following these guidelines helps communicate that you respect the time
of the developers who work on GJS.
In return, they should reciprocate that respect in addressing your
issue, reviewing your work, and helping finalize your merge requests.

### What kinds of contributions we are looking for ###

There are many ways to contribute to GJS, not only writing code.
We encourage all of them.
You can write example programs, tutorials, or blog posts; improve the
documentation; [submit bug reports and feature requests][bugtracker];
triage existing bug reports; vote on issues with a thumbs-up or
thumbs-down; or write code which is incorporated into [GJS itself][gjs].

### What kinds of contributions we are not looking for ###

Please don't use the [issue tracker][bugtracker] for support questions.
Instead, check out the [#javascript][chat] chat channel on Matrix.
You can also try the [GNOME Discourse][discourse] forum, or
Stack Overflow.

If you are writing code, please do not submit merge requests that only
fix linter errors in code that you are not otherwise changing (unless
you have discussed it in advance with a maintainer on [Matrix][chat].)

When writing code or submitting a feature request, make sure to first
read the section below titled "Roadmap".
Contributions that run opposite to the roadmap are not likely to be
accepted.

## Ground Rules ##

Your responsibilities as a contributor:

- Be welcoming and encouraging to newcomers.
- Conduct yourself professionally; rude, abusive, harassing, or
  discriminatory behaviour is not tolerated.
- For any major changes and enhancements you want to make, first create
  an issue in the [bugtracker], discuss things transparently, and get
  community feedback.
- Ensure all jobs are green on GitLab CI for your merge requests.
- Your code must pass the tests. Sometimes you can experience a runner
  system failure which can be fixed by re-running the job.
- Your code must pass the linters; code should not introduce any new
  linting errors.
- Your code should not cause any compiler warnings.
- Add tests for any new functionality you write, and regression tests
  for any bugs you fix.

## Your First Contribution ##

Unsure where to start?

Try looking through the [issues labeled "Newcomers"][newcomers].
We try to have these issues contain some step-by-step explanation on how
a newcomer might approach them.
If that explanation is missing from an issue marked "Newcomers", feel
free to leave a comment on there asking for help on how to get started.

[Issues marked "Help Wanted"][helpwanted] may be a bit more involved
than the Newcomers issues, but many of them still do not require
in-depth familiarity with GJS.

If you're applying to work on GJS for Outreachy or Summer of Code, see
our [Internship Getting Started][internship] documentation.

## How to contribute documentation or tutorials ##

If you don't have an account on [gitlab.gnome.org], first create one.

Some contributions are done in different places than the main GJS
repository.
To contribute to the documentation, go to the [DevDocs][devdocs]
repository.
To contribute to tutorials, go to [GJS Guide][gjsguide].

Next, read the [workflow guide to contributing to GNOME][workflow].
(In short, create a fork of the repository, make your changes on a
branch, push them to your fork, and create a merge request.)

When you submit your merge request, make sure to click "Allow commits
from contributors with push access".
This is so that the maintainers can re-run the GitLab CI jobs, since
there is currently a bug in the infrastructure that makes some of the
jobs fail unnecessarily.

!157 is an example of a small documentation bugfix in a merge request.

That's all!

## How to contribute code ##

To contribute code, follow the instructions above for contributing
documentation.
There are further instructions for how to set up a development
environment and install the correct tools for GJS development in the
[Hacking.md][hacking] file.

## How to report a bug ##

If you don't have an account on [gitlab.gnome.org], first create one.
Go to the [issue tracker][bugtracker] and click "New issue".

Use the "bug" template when reporting a bug.
Make sure to answer the questions in the template, as otherwise it might
make your bug harder to track down.

_If you find a security vulnerability,_ make sure to mark the issue as
"confidential"!

If in doubt, ask on [Matrix][chat] whether you should report a bug about
something, but generally it's OK to just go ahead.

Bug report #170 is a good example of a bug report with an independently
runnable code snippet for testing, and lots of information, although it
was written before the templates existed.

## How to suggest a feature or enhancement ##

If you find yourself wishing for a feature that doesn't exist in GJS,
you are probably not alone.
Open an issue on our [issue tracker][bugtracker] which describes the
feature you would like to see, why you need it, and how it should work.
Use the "feature" template for this.
However, for a new feature, the likelihood that it will be implemented
goes way up if you or someone else plans to submit a merge request along
with it.

If the feature is small enough that you won't feel like your time was
wasted if we decide not to adopt it, you can just submit a merge
request rather than going to the issue tracker.
Make sure to explain why you think it's a good feature to have!
!213 is an example of a small feature suggestion that was submitted as a
merge request.

In cases where you've seen something that needs to be fixed or
refactored in the code, it's OK not to use a template.
It's OK to be less rigorous here, since this type of report is usually
used by people who plan to fix the issue themselves later.

## How to triage bugs ##

You can help the maintainers by examining the existing bug reports in
the bugtracker and adding instructions to reproduce them, or
categorizing them with the correct labels.

For bugs that cause a crash (segmentation fault, not just a JS
exception) use the "1. Crash" label.
For other bugs, use the "1. Bug" label.
Feature requests should get the "1. Feature" label.
Any crashes, or bugs that prevent most or all users from using GJS or
GNOME Shell, should also get the "To Do" label.

If some information is missing from the bug (for example, you can't
reproduce it based on their instructions,) add the "2. Needs
information" label.

Add any topic labels from the "5" group (e.g. "5. Performance") as you
see fit.

As for reproducer instructions, a small, self-contained JS program that
exhibits the bug, to be run with the command-line `gjs` interpreter, is
best.
Instructions that provide code to be loaded as a GNOME Shell extension
are less helpful, because they are more tedious to test.

## Code review process ##

Once you have submitted your merge request, a maintainer will review it.
You should get a first response within a few days.
Sometimes maintainers are busy; if it's been a week and you've heard
nothing, feel free to ping the maintainer and ask for an estimate of
when they might be able to review the merge request.

You might get a review even if some of the GitLab CI jobs have not yet
succeeded.
In that case, acceptance of the merge request is contingent on fixing
whatever needs to be fixed to get all the jobs to turn green.

In general, unless the merge request is very simple, it will not be
ready to accept immediately.
You should normally expect one to three rounds of code review, depending
on the size and complexity of the merge request.
Be prepared to accept constructive criticism on your code and to work on
improving it before it's merged; code review comments don't mean it's
bad.

!242 is an example of a bug fix merge request with a few code review
comments on it, if you want to get a feel for the process.

Contributors with a GNOME developer account have automatic push access
to the main GJS repository.
However, even if you have this access, you are still expected to submit
a merge request and have a GJS maintainer review it.
The exception to this is if there is an emergency such as GNOME
Continuous being broken.

## Community ##

For general questions and support, visit the [#javascript][chat] channel
on Matrix.

The maintainers are listed in the [DOAP file][doap] in the root of the
repository.

## Roadmap and Philosophy ##

This section explains what kinds of changes we do and don't intend to
make in GJS in the future and what direction we intend to take the
project.

Internally, GJS uses Firefox's Javascript engine, called SpiderMonkey.

First of all, we will not consider switching GJS to use a different
Javascript engine.
If you believe that should be done, the best way to make it happen is to
start a new project, copy GJS's regression test suite, and make sure all
the tests pass and you can run GNOME Shell with it.

Every year when a new ESR (extended support release) of Firefox appears,
we try to upgrade GJS to use the accompanying version of SpiderMonkey
as soon as possible.
Sometimes upgrading SpiderMonkey requires breaking backwards
compatibility, and in that case we try to make it as easy as possible
for existing code to adapt.

Other than the above exception, we avoid all changes that break existing
code, even if they would be convenient.
However, it is OK to break compatibility with GJS's documented behaviour
if in practice the behaviour never actually worked as documented.
(That happens more often than you might think.)

We also try to avoid surprises for people who are used to modern ES
standard Javascript, so custom GJS classes should not deviate from the
behaviour that people would be used to in the standard.

The Node.js ecosystem is quite popular and many Javascript developers
are accustomed to it.
In theory, we would like to move in the direction of providing all the
same facilities as Node.js, but we do not necessarily want to copy the
exact way things work in Node.js.
The platforms are different and so the implementations sometimes need to
be different too.

The module system in GJS should be considered legacy.
We don't want to make big changes to it or add any features.
Instead, we want to enable ES6-style imports for the GJS platform.

We do have some overrides for GNOME libraries such as GLib, to make
their APIs more Javascript-like.
However, we like to keep these to a minimum, so that GNOME code remains
straightforward to read if you are used to using the GNOME libraries in
other programming languages.

GJS was originally written in C, and the current state of the C++ code
reflects that.
Gradually, we want to move the code to a more idiomatic C++ style, using
smart pointer classes such as `Gjs::AutoChar` to help avoid memory
leaks.
Even farther in the future, we expect the Rust bindings for SpiderMonkey
to mature as Mozilla's Servo browser engine progresses, and we may
consider rewriting part or all of GJS in Rust.

We believe in automating as much as possible to prevent human error.
GJS is a complex program that powers a lot of GNOME, so breakages can be
have far-reaching effects in other programs.
We intend to move in the direction of having more static code checking
in the future.
We would also like to have more automated integration testing, for
example trying to start a GNOME Shell session with each new change in
GJS.

Lastly, changes should in principle be compatible with other platforms
than only Linux and GNOME.
Although we don't have automated testing for other platforms, we will
occasionally build and test things there, and gladly accept
contributions to fix breakages on other platforms.

## Conventions ##

### Coding style ###

We use the [Google style guide][googlestyle] for C++ code, with a few
exceptions, 4-space indents being the main one.
There is a handy git commit hook that will autoformat your code when you
commit it; see the [Hacking.md][hacking] file.

For C++ coding style concerns that can't be checked with a linter or an
autoformatter, read the [CPP_Style_Guide.md][cppstyle] file.

For Javascript code, an [ESLint configuration file][eslint] is included
in the root of the GJS repository.
This is not integrated with a git commit hook, so you need to manually
make sure that all your code conforms to the style.
Running `./tools/run_eslint.sh --fix` should autoformat most of your
JavaScript code correctly.

### Commit messages ###

The title of the commit should say what you changed, and the body of the
commit message should explain why you changed it.
We look in the commit history quite often to figure out why code was
written a certain way, so it's important to justify each change so that
in the future people will realize why it was needed.

For further guidelines about line length and commit messages, read
[this guide][commitmessages].

If the commit is related to an open issue in the issue tracker, note
that on the last line of the commit message. For example, `See #153`, or
`Closes #277` if the issue should be automatically closed when the merge
request is accepted. Otherwise, creating a separate issue is not required.

## Thanks ##

Thanks to [@nayafia][contributingtemplate] for the inspiration to write
this guide!

[gitlab.gnome.org]: https://gitlab.gnome.org
[bugtracker]: https://gitlab.gnome.org/GNOME/gjs/issues
[gjs]: https://gitlab.gnome.org/GNOME/gjs
[chat]: https://matrix.to/#/#javascript:gnome.org
[discourse]: https://discourse.gnome.org/
[newcomers]: https://gitlab.gnome.org/GNOME/gjs/issues?label_name%5B%5D=4.+Newcomers
[helpwanted]: https://gitlab.gnome.org/GNOME/gjs/issues?label_name%5B%5D=4.+Help+Wanted
[internship]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/doc/Internship-Getting-Started.md
[devdocs]: https://github.com/ptomato/devdocs
[gjsguide]: https://gitlab.gnome.org/rockon999/gjs-guide
[workflow]: https://wiki.gnome.org/GitLab#Using_a_fork_-_Non_GNOME_developer
[hacking]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/doc/Hacking.md
[doap]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/gjs.doap
[googlestyle]: https://google.github.io/styleguide/cppguide.html
[cppstyle]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/doc/CPP_Style_Guide.md
[eslint]: https://eslint.org/
[commitmessages]: https://chris.beams.io/posts/git-commit/
[contributingtemplate]: https://github.com/nayafia/contributing-template
