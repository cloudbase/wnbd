==========================
Submitting Patches to WNBD
==========================

If you have a patch that fixes an issue, feel free to open a GitHub pull request
("PR") targeting the "master" branch, but do read this document first, as it
contains important information for ensuring that your PR passes code review
smoothly.

.. contents::
   :depth: 3


Sign your work
--------------

The sign-off is a simple line at the end of the explanation for the
commit, which certifies that you wrote it or otherwise have the right to
pass it on as a open-source patch. The rules are pretty simple: if you
can certify the below:

Developer's Certificate of Origin 1.1
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.

then you just add a line saying ::

        Signed-off-by: Random J Developer <random@developer.example.org>

using your real name (sorry, no pseudonyms or anonymous contributions.)

Git can sign off on your behalf
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Please note that git makes it trivially easy to sign commits. First, set the
following config options::

    $ git config --list | grep user
    user.email=my_real_email_address@example.com
    user.name=My Real Name

Then just remember to use ``git commit -s``. Git will add the ``Signed-off-by``
line automatically.


Separate your changes
---------------------

Group *logical changes* into individual commits.

If you have a series of bulleted modifications, consider separating each of
those into its own commit.

For example, if your changes include both bug fixes and performance enhancements
for a single component, separate those changes into two or more commits. If your
changes include an API update, and a new feature which uses that new API,
separate those into two patches.

On the other hand, if you make a single change that affects numerous
files, group those changes into a single commit. Thus a single logical change is
contained within a single patch. (If the change needs to be backported, that
might change the calculus, because smaller commits are easier to backport.)


Describe your changes
---------------------

Each commit has an associated commit message that is stored in git. The first
line of the commit message is the `commit title`_. The second line should be
left blank. The lines that follow constitute the `commit message`_.

A commit and its message should be focused around a particular change.

Commit title
^^^^^^^^^^^^

The text up to the first empty line in a commit message is the commit
title. It should be a single short line of at most 72 characters,
summarizing the change, and prefixed with the module you are changing.
Also, it is conventional to use the imperative mood in the commit title.
Positive examples include::

     wnbd-client: Use wnbd.dll

Some negative examples (how *not* to title a commit message)::

     update driver
     driver bug fix
     fix issue 99999

Further to the last negative example ("fix issue 99999"), see `Fixes line(s)`_.

Commit message
^^^^^^^^^^^^^^

(This section is about the body of the commit message. Please also see
the preceding section, `Commit title`_, for advice on titling commit messages.)

In the body of your commit message, be as specific as possible. If the commit
message title was too short to fully state what the commit is doing, use the
body to explain not just the "what", but also the "why".

For positive examples, peruse ``git log`` in the ``master`` branch. A negative
example would be a commit message that merely states the obvious. For example:
"this patch includes updates for component X. Please apply."

Fixes line(s)
^^^^^^^^^^^^^

If the commit fixes one or more issues tracked through Github issues,
add a ``Fixes:`` line (or lines) to the commit message, to connect this change
to addressed issue(s) - for example::

     Fixes: #15

This line should be added just before the ``Signed-off-by:`` line (see `Sign
your work`_).

It helps reviewers to get more context of this bug and facilitates updating of
the issue status.

Here is an example showing a properly-formed commit message::

     wnbd-client: add "--foo" option to the bar command

     This commit updates the bar command, adding the "--foo" option.

     Fixes: #45
     Signed-off-by: Random J Developer <random@developer.example.org>

If a commit fixes a regression introduced by a different commit, please also
(in addition to the above) add a line referencing the SHA1 of the commit that
introduced the regression. For example::

     Fixes: 9dbe7a003989f8bb45fe14aaa587e9d60a392727


PR best practices
-----------------

PRs should be opened on branches contained in your fork of
https://github.com/cloudbase/wnbd.git - do not push branches directly to
``cloudbase/wnbd.git``.

PRs should target "master".

In addition to a base, or "target" branch, PRs have several other components:
the `PR title`_, the `PR description`_, labels, comments, etc. Of these, the PR
title and description are relevant for new contributors.

PR title
^^^^^^^^

If your PR has only one commit, the PR title can be the same as the commit title
(and GitHub will suggest this). If the PR has multiple commits, do not accept
the title GitHub suggest. Either use the title of the most relevant commit, or
write your own title. In the latter case, use the same "module: short
description" convention described in `Commit title`_ for the PR title, with
the following difference: the PR title describes the entire set of changes,
while the `Commit title`_ describes only the changes in a particular commit.

PR description
^^^^^^^^^^^^^^

In addition to a title, the PR also has a description field, or "body".

The PR description is a place for summarizing the PR as a whole. It need not
duplicate information that is already in the commit messages. It can contain
notices to maintainers, links to Github issues and other related information,
to-do lists, etc. The PR title and description should give readers a high-level
notion of what the PR is about, quickly enabling them to decide whether they
should take a closer look.

Test your changes
-----------------

Before opening your PR, it's a good idea to run tests on your patchset.

The most simple test is to verify that your patchset builds, at least in your
own development environment.

Document your changes
---------------------

At the moment, most of the WNBD documentation consists in the readme file.
Please make sure to update it whenever your changes require it.
