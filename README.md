hexplore-client-debian
======================

Debian packaging for Hexplore client

This repository stores the debian/ubuntu packaging machinery
for the Hexplore client.  Note that per debian packaging
guidelines, the actual client sources are stored elsewhere
and obtained by means of a pristine tarball.

Upstream Export
===============
git archive HEAD --prefix hexplore-client-0.3.0/ -o /tmp/hexplore-client-0.3.0_$(git rev-parse HEAD).tar.gz hexcom server/wire client

Unpacking
=========
put the revision tag in .git/index


Prerequisites
=============
These are needed to set up a build machine sufficient for
building this package:

devscripts
xcftools
quilt
git-buildpackage
