all: subdirs thisdir

clean:

subdirs:
	@for i in a cmd f op plugin; do (cd $$i; make); done

thisdir: kwlist.js INTRO.html jx_man.html

.ALWAYS kwlist.js:
	sh scankeyword.sh

INTRO.html: INTRO.html.template
	sh scan.sh

jx_man.html: ../man/jx.1
	groff -t -man -Thtml ../man/jx.1 >jx_man.html
