onekeyirc 0.1.35 - LANGUAGE PACK r2
============================

Requires onekeyirc-0.1.35-english-source-r1.tar.gz (not the old historic
onekeyirc-0.1.35-source.tar.gz). Do not mix source revisions.

Extract both archives into the same parent directory, then run:
  sh onekeyirc-0.1.35-languages-r2/apply.sh onekeyirc-0.1.35
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Manual source copy (does not verify fingerprints):
  cp -R onekeyirc-0.1.35-languages-r2/overlay/. onekeyirc-0.1.35/
Only use this with a fresh, unmodified English r1 source tree before
configuring. This pack is not a standalone program.

The program's version stays at 0.1.35. Identify the source as
english-r1 and localized build as language-pack-r2.

Languages: Indonesian, Malay, Turkish, Spanish, Portuguese, French,
German, Russian, Italian, Arabic. These are drafts awaiting review;
some detailed help and all full manuals remain English. No translation
of IRC protocol tokens or configuration syntax is performed.
