(define-library (srfi s170 posix)
  (import (curry posix) (scheme base))
  (export
    file-info file-info? file-info:device file-info:inode file-info:mode
    file-info:nlinks file-info:uid file-info:gid file-info:rdev
    file-info:size file-info:blksize file-info:blocks
    file-info:atime file-info:mtime file-info:ctime
    file-info-directory? file-info-regular? file-info-symlink?
    file-info-fifo? file-info-socket? file-info-device? file-info-char-device?

    create-directory delete-directory directory-files
    open-directory read-directory close-directory

    rename-file create-hard-link create-symlink read-symlink real-path
    truncate-file set-file-mode set-file-owner set-file-times

    umask set-umask! current-directory set-current-directory!
    pid nice

    user-uid user-gid user-effective-uid user-effective-gid
    user-supplementary-gids

    user-info user-info? user-info:name user-info:uid user-info:gid
    user-info:home-dir user-info:shell user-info:full-name
    user-info:parsed-full-name

    group-info group-info? group-info:name group-info:gid

    posix-time monotonic-time

    set-environment-variable! delete-environment-variable!

    terminal?

    owner/unchanged group/unchanged)
  (begin
    ; Most identifiers are already named per the SRFI-170 spec and delegate
    ; directly to the C-level (curry posix) module — re-exported here only
    ; so portable code using the (srfi s170 posix) naming convention
    ; works unchanged. Requires curry built with -DBUILD_MODULE_POSIX=ON
    ; (the default). See docs/reference/module-posix.md for the (curry
    ; posix) scope this covers and deliberately leaves out.
    ;
    ; Three names below have no (curry posix) counterpart and are defined
    ; here instead, spec-compliance sugar rather than core POSIX surface:

    ; owner/unchanged and group/unchanged -- pass either as the
    ; corresponding argument to set-file-owner to leave that half alone.
    ; Both are -1, the same sentinel chown(2) itself treats as "don't
    ; change this id" -- set-file-owner's C implementation (fn_set_file_
    ; owner, modules/posix/posix.c) already passes uid/gid straight
    ; through to chown() with no translation, so passing -1 explicitly
    ; here needs no C-side change at all.
    (define owner/unchanged -1)
    (define group/unchanged -1)

    ; user-info:parsed-full-name -- the GECOS field (user-info:full-name)
    ; is conventionally comma-separated (full name, office, work phone,
    ; home phone -- the layout SRFI-170's own "parsed" variant is defined
    ; against); this returns just the full-name portion, up to the first
    ; comma (or the whole string if there is none). A minority of systems
    ; instead write GECOS as "Lastname, Firstname" with no office/phone
    ; fields at all -- on those, this returns just the surname. There is
    ; no reliable way to distinguish the two conventions from the string
    ; alone; this matches the convention SRFI-170 itself assumes.
    (define (user-info:parsed-full-name info)
      (let* ((full (user-info:full-name info))
             (comma (%string-index full #\,)))
        (if comma (substring full 0 comma) full)))

    (define (%string-index s ch)
      (let ((len (string-length s)))
        (let loop ((i 0))
          (cond ((= i len) #f)
                ((char=? (string-ref s i) ch) i)
                (else (loop (+ i 1)))))))
    ))
