<!DOCTYPE style-sheet PUBLIC "-//James Clark//DTD DSSSL Style Sheet//EN" [
<!ENTITY docbook.dsl PUBLIC "-//Norman Walsh//DOCUMENT DocBook HTML Stylesheet//EN" CDATA dsssl>
]>

<style-sheet>

<style-specification id="html" use="docbook">
<style-specification-body> 
    ;;
    ;;  Tweak a few options from the default HTML stylesheet
    ;;
    (define %html-ext% ".html")
    (define %body-attr% '())
    (define %shade-verbatim% #t)
    (define %use-id-as-filename% #t)

    (define %graphic-default-extension% "gif")
    (define %admon-graphics% #t)
    (define %gentext-nav-tblwidth% "100%")

    (define %generate-article-toc% #t)
    (define %generate-article-titlepage-on-separate-page% #t)
    (define %generate-article-toc-on-titlepage% #f)

</style-specification-body>
</style-specification>

<style-specification id="onepage" use="docbook">
<style-specification-body> 
    ;;
    ;;  Much the same as above except all in one file
    ;;
    (define nochunks #t)
    (define %html-manifest% #f)
    (define %use-id-as-filename% #f)

    (define %html-ext% ".html")
;    (define %body-attr% '())
;    (define %shade-verbatim% #t)
    ;; (define %use-id-as-filename% #t)

;    (define %graphic-default-extension% "gif")
;    (define %admon-graphics% #t)
;    (define %gentext-nav-tblwidth% "100%")

;    (define %generate-article-toc% #t)
    ;; (define %generate-article-titlepage-on-separate-page% #t)
    ;; (define %generate-article-toc-on-titlepage% #f)

</style-specification-body>
</style-specification>

<external-specification id="docbook" document="docbook.dsl">

</style-sheet>
