(define-library (srfi srfi-170)
  (import (srfi s170 posix))
  (export
    file-info file-info? file-info:device file-info:inode file-info:mode
    file-info:nlinks file-info:uid file-info:gid file-info:rdev
    file-info:size file-info:blksize file-info:blocks file-info:atime
    file-info:mtime file-info:ctime file-info-directory? file-info-regular?
    file-info-symlink? file-info-fifo? file-info-socket? file-info-device?
    file-info-char-device? create-directory delete-directory directory-files
    open-directory read-directory close-directory rename-file
    create-hard-link create-symlink read-symlink real-path truncate-file
    set-file-mode set-file-owner set-file-times umask set-umask!
    current-directory set-current-directory! pid nice user-uid user-gid
    user-effective-uid user-effective-gid user-supplementary-gids user-info
    user-info? user-info:name user-info:uid user-info:gid user-info:home-dir
    user-info:shell user-info:full-name group-info group-info?
    group-info:name group-info:gid posix-time monotonic-time
    set-environment-variable! delete-environment-variable! terminal?))
