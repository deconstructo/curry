" Curry Scheme filetype plugin.
if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal commentstring=;\ %s
setlocal comments=:;;;,:;;,:;
setlocal lisp
setlocal lispwords=define,define-syntax,define-values,define-record-type,
      \define-library,define-rule,define-ruleset,define-algebra,
      \lambda,let,let*,letrec,letrec*,let-values,let*-values,
      \let-syntax,letrec-syntax,when,unless,do,case,cond,
      \parameterize,guard,with-exception-handler,with-assumptions,
      \syntax-rules,library,receive,spawn

let b:undo_ftplugin =
      \ "setlocal commentstring< comments< lisp< lispwords<"
