((auto-mode-alist . (("/.git/COMMIT_EDITMSG\\'" . diff-mode)))
 (nil . ((mode . electric-quote-local)
         (make-backup-files . nil)

         (delete-trailing-whitespace . t)
         (eval . (when (derived-mode-p 'text-mode 'prog-mode 'conf-mode)
                   (add-hook 'before-save-hook
                             #'delete-trailing-whitespace
                             nil "local")))

         (require-final-newline . t)

         (eval . (line-number-mode -1))
         (mode . display-line-numbers)

         (mode . column-number)

         (indent-tabs-mode . nil)

         (sentence-end-double-space . t)

         (eval . (electric-indent-local-mode -1))

         (treesit-font-lock-level . 4)

         (whitespace-style . (face tabs))
         (mode . whitespace)))
 (makefile-mode . ((indent-tabs-mode . t)))
 (c++-ts-mode . ((mode . electric-pair-local))))
