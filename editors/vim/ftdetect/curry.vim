" Curry Scheme filetype detection.
" *.scm / *.sld default to Vim's builtin scheme filetype; curry takes over
" when the shebang names curry, or globally with:
"   let g:filetype_scm = "curry"
augroup filetypedetect
  autocmd BufNewFile,BufRead *.scm,*.sld call s:CurryDetect()
augroup END

function! s:CurryDetect() abort
  if getline(1) =~# '^#!.*\<curry\>'
    setlocal filetype=curry
  elseif get(g:, 'filetype_scm', '') ==# 'curry'
    setlocal filetype=curry
  endif
endfunction
