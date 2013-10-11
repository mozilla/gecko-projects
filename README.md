integration-gecko-projects
==========================

This repository is synced from hg.mozilla.org and should be read-only.
It is intended to be used with integration-gecko-dev.  For instance, to add the 'alder' branch to your local repository:

    git clone https://github.com/mozilla/integration-gecko-dev.git
    cd integration-gecko-dev
    # Add a new 'projects' remote
    git remote add projects https://github.com/mozilla/integration-gecko-projects.git
    # Add a new branch from integration-gecko-projects.  For this example, let's say 'alder'.
    BRANCH=alder
    # Set the list of branches we care about; this will discard any previous branches
    git remote set-branches projects $BRANCH
    # To append to the list, use --add:
    #     git remote set-branches --add projects $BRANCH
    git fetch projects
    git checkout $BRANCH

Since the mercurial repositories being synced have less strict rules, there is a higher likelihood of a commit that may cause sync issues.  Mozilla Release Engineering reserves the right to reset this repo and resync at any point, should it become necessary.
