# Version from the latest reachable git tag ("v2.0.0" -> "2.0.0"), so a
# release is just `git tag -a vX.Y.Z -m vX.Y.Z`, no source edit/commit.
# Built exactly at the tag: "2.0.0"; commits after it: the short hash of
# the commit is appended, "2.0.0-0566e34".
# Falls back to 0.0.0 without git, outside a checkout or with no tag
# reachable (e.g. a shallow clone without tags).
# Taken when qmake runs, like the git commit and the build time: rerun
# qmake after a commit or a tag (Qt Creator: Build > Run qmake; command
# line: qmake -r, a plain qmake on the subdirs project keeps app's
# version.h).
# Generates version.h in the build dir from version.h.in (there \" is a
# quote: qmake drops plain quotes in QMAKE_SUBSTITUTES).
# tools/package/make_package.sh computes the same version.

RS_GIT_HASH = $$system(git -C $$RS_SRC rev-parse --short HEAD 2>/dev/null)

RS_VERSION = 0.0.0
RS_GIT_TAG = $$system(git -C $$RS_SRC describe --tags --abbrev=0 2>/dev/null)
RS_GIT_TAG ~= s/^v//
contains(RS_GIT_TAG, "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?(\\.[0-9]+)?$") {
    RS_VERSION = $$RS_GIT_TAG
} else:!isEmpty(RS_GIT_TAG) {
    warning("latest git tag '$$RS_GIT_TAG' is not a plain X.Y.Z version, using $$RS_VERSION")
}
!isEmpty(RS_GIT_HASH):!system(git -C $$RS_SRC describe --tags --exact-match HEAD >/dev/null 2>&1): \
    RS_VERSION = $${RS_VERSION}-$${RS_GIT_HASH}

# Short hash of the commit, "-dirty" with uncommitted changes
isEmpty(RS_GIT_HASH) {
    RS_GIT_COMMIT = unknown
} else {
    RS_GIT_COMMIT = $$RS_GIT_HASH
    !system(git -C $$RS_SRC diff-index --quiet HEAD --): RS_GIT_COMMIT = $${RS_GIT_COMMIT}-dirty
}

RS_BUILD_TIMESTAMP = $$system(date -u \"+%Y-%m-%d %H:%M:%S UTC\")

rs_version.input  = $$RS_SRC/version.h.in
rs_version.output = $$OUT_PWD/version.h
QMAKE_SUBSTITUTES += rs_version
INCLUDEPATH += $$OUT_PWD
