# Welcome to GJS!

This document is a guide to getting started with GJS, especially if you
are applying to an internship program such as Outreachy or Summer of
Code where you make a contribution as part of your application process.

GJS is the JavaScript environment inside GNOME.
It's responsible for executing the user interface code in the GNOME
desktop, including the extensions that people use to modify their
desktop with.
It's also the environment that several GNOME apps are written in, like
GNOME Sound Recorder, Polari, etc.
GJS is written in both C++ and JavaScript, and is built on top of the
JavaScript engine from Firefox, called SpiderMonkey.

The application process is supposed to give you the opportunity to work
on good newcomer bugs from GJS.

> Thanks to Iain Ireland for kind permission to adapt this document from
> SpiderMonkey's instructions!

## Steps to participate

### Phase 1: Getting set up

There are two parts of this phase: getting your development environment
set up, and getting set up to communicate with the other GNOME
volunteers.
For your development environment, the tasks are:

1. Make an account on [GitLab](https://gitlab.gnome.org).
1. Download the GJS source code and build GJS.
    > You can follow the [GNOME Newcomer
    > instructions](https://wiki.gnome.org/Newcomers/BuildProject) if
    > you want to use Builder, or follow the [instructions](Hacking.md)
    > for the command line.
1. Run the GJS test suite and make sure it passes.
    > Run `meson test -C _build`.
    > If you are using Builder, do this in a runtime terminal
    > (Ctrl+Alt+T).

For communication, your tasks are:

1. Create an account on [Matrix](https://gnome.element.io).
1. Introduce yourself in
   [#javascript](https://matrix.to/#/#javascript:gnome.org)!

Congratulations! Now you’re ready to contribute!

### Phase 2: Fixing your first bug

1. Find an unclaimed ["Newcomers" bug in the GJS
   bugtracker](https://gitlab.gnome.org/GNOME/gjs/-/issues?label_name%5B%5D=4.+Newcomers).
1. Post a comment on your bug to say that you're working on it.
    > If you're an Outreachy or Summer of Code participant, make sure to
    > mention that!

    > Please only claim bugs if you are actively working on them.
    > We have a limited number of newcomers bugs.
1. Work on the bug.
1. If you get stuck, ask questions in Matrix or as a comment on the bug.
   See below for advice on asking good questions.
1. Once your patch is complete and passes all the tests, make a merge
   request with GitLab.
1. If any CI results on the merge request are failing, look at the error
   logs and make the necessary changes to turn them green.
    > If this happens, it's usually due to formatting errors.
1. The project mentor, and maybe others as well, will review the code.
   Work with them to polish up the patch and get it ready to merge.
   When it's done, the mentor will merge it.
    > It's normal to have a few rounds of review.

Congratulations! You've contributed to GNOME!

### Phase 3: Further contributions

If you are applying to an internship and would like to boost your
application with additional contributions, you can find another bug and
start the process again.

We're doing our best to make sure that we have enough newcomers bugs
available for our applicants, but they tend to get fixed quickly during
the internship application periods.
If you've already completed an easier bug, please pick a slightly harder
bug for your next contribution.

## Evaluation dimensions

We **will** be evaluating applicants based on the following criteria:

1. **Communication:** When collaborating remotely, communication is
   critical.
   Does the applicant ask good questions?
   Does the applicant write good comments?
   Can the applicant clearly explain any challenges they are facing?
1. **Learning on the fly:** How quickly can the applicant ramp up on a
   new topic?
   Is the applicant willing to sit with and struggle through challenging
   technical problems?
1. **Programming knowledge:** You don't have to be a wizard, but you
   should feel reasonably comfortable with programming in the languages
   that will be mainly used during the project.
   Is the applicant able to reliably produce merge requests that pass CI
   with moderate feedback?
   Does the applicant have a basic understanding of how to debug
   problems?

We **will not** be evaluating applicants based on the following
criteria:

1. **Geographic location:** GNOME contributors come from everywhere, and
   we regularly collaborate across significant time zone gaps.
   Communication may have to be more asynchronous for applicants in some
   time zones, but we will not be making a decision based on location.
1. **Formal qualifications / schooling**: We will be evaluating
   applicants only based on their contributions during the application
   process.

## Asking good questions

[This blog post by Julia Evans](https://jvns.ca/blog/good-questions/) is
an excellent resource on asking good questions.
(The "decide who to ask" section is less relevant in the context of
Outreachy or Summer of Code; during the application process, you should
generally be asking questions in Matrix, and whoever sees your question
first and knows the answer will respond.)

Good questions should respect the time of both the person answering the
question, **and the person asking it** (you yourself!).
You shouldn't flood the Matrix channel asking questions that you could
answer yourself in a short time with a search engine.
On the other hand, you should also not spend days trying to figure out
the answer to something that somebody more experienced could clear up in
a few minutes.

If you are having problems, it is often useful to take a break and come
back with a fresh head.
If you're still stuck, it's amazing how often the answer will come to
you as you try to write your question down.
If you've managed to write out a clear statement of your problem, and
you still can't figure out the answer: ask!
