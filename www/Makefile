all: subdirs thisdir

clean:

subdirs:
	@for i in a cmd f op plugin; do (cd $$i; make); done

thisdir: kwlist.js INTRO.html jsoncalc_man.html

.ALWAYS kwlist.js:
	sh scankeyword.sh

INTRO.html: INTRO.html.template
	sh scan.sh

jsoncalc_man.html: ../man/jsoncalc.1
	groff -t -man -Thtml ../man/jsoncalc.1 >jsoncalc_man.html
