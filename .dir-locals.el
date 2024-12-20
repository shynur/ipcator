;; Emacs 仅在编辑 Git commit message 的时候
;; 会被唤起, 所以此处的配置仅针对上述场景.

((auto-mode-alist . (("/.git/COMMIT_EDITMSG\\'" . diff-mode)))
 (nil . ((mode . electric-quote-local)
         (make-backup-files . nil))))
