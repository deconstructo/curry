/*
 * curry_git — libgit2 bindings for Curry Scheme.
 *
 * Requires: libgit2 (sudo apt install libgit2-dev)
 *
 * Scheme API:
 *   ; Repository
 *   (git-open   path)                   -> repo
 *   (git-init   path)                   -> repo
 *   (git-clone  url dest-path)          -> repo
 *   (git-close! repo)                   -> void
 *
 *   ; Status
 *   (git-status repo)                   -> alist ((path . flags) ...)
 *   (git-head   repo)                   -> string (branch name or OID)
 *
 *   ; Log / commits
 *   (git-log    repo)                   -> list of commit alists
 *   (git-log    repo count)             -> list of commit alists (limited)
 *   ; Each commit alist: ((id . sha) (message . str) (author . str)
 *   ;                     (email . str) (time . integer))
 *
 *   ; Index (staging area)
 *   (git-add!   repo path)              -> void   (stage a file)
 *   (git-add-all! repo)                 -> void   (git add -A)
 *   (git-reset-file! repo path)         -> void   (unstage)
 *
 *   ; Commits
 *   (git-commit! repo message author-name author-email) -> string (oid)
 *
 *   ; Branches
 *   (git-branches repo)                 -> list of strings
 *   (git-current-branch repo)           -> string
 *   (git-checkout! repo branch)         -> void
 *   (git-branch-create! repo name)      -> void
 *
 *   ; Diff
 *   (git-diff repo)                     -> string (patch text, unstaged)
 *   (git-diff-staged repo)              -> string (patch text, staged)
 *
 *   ; Tags
 *   (git-tags repo)                     -> list of strings
 *   (git-tag-create! repo name message) -> void
 *
 *   ; Remote
 *   (git-remotes repo)                  -> list of (name . url) pairs
 *   (git-fetch!  repo remote)           -> void
 *   (git-pull!   repo remote branch)    -> void
 *   (git-push!   repo remote branch)    -> void
 */

#include <curry.h>
#include <git2.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* libgit2 API compatibility shims */
#ifndef GIT_OID_SHA1_HEXSIZE
#  define GIT_OID_SHA1_HEXSIZE GIT_OID_HEXSZ   /* renamed in 1.5 */
#endif
#ifndef git_strarray_dispose
#  define git_strarray_dispose git_strarray_free /* renamed in 1.4 */
#endif

/* ---- Repo handle ---- */

static curry_val repo_tag(void) { return curry_make_symbol("git-repo"); }

static curry_val repo_to_val(git_repository *r) {
    curry_val bv = curry_make_bytevector(sizeof(git_repository *), 0);
    for (size_t i = 0; i < sizeof(git_repository *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&r)[i]);
    return curry_make_pair(repo_tag(), bv);
}

static git_repository *val_to_repo(curry_val v) {
    if (!curry_is_pair(v) || curry_car(v) != repo_tag())
        curry_error("git: not a repository handle");
    curry_val bv = curry_cdr(v);
    git_repository *r;
    for (size_t i = 0; i < sizeof(git_repository *); i++)
        ((uint8_t *)&r)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return r;
}

static void git_check(int rc, const char *ctx) {
    if (rc < 0) {
        const git_error *e = git_error_last();
        curry_error("git %s: %s", ctx, e ? e->message : "(unknown error)");
    }
}

/* ---- Repository ---- */

static curry_val fn_open(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = NULL;
    git_check(git_repository_open(&repo, curry_string(av[0])), "open");
    return repo_to_val(repo);
}

static curry_val fn_init(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = NULL;
    git_check(git_repository_init(&repo, curry_string(av[0]), 0), "init");
    return repo_to_val(repo);
}

static curry_val fn_clone(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = NULL;
    git_check(git_clone(&repo, curry_string(av[0]), curry_string(av[1]), NULL), "clone");
    return repo_to_val(repo);
}

static curry_val fn_close(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository_free(val_to_repo(av[0]));
    return curry_void();
}

/* ---- Status ---- */

static curry_val fn_status(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);

    git_status_list *list = NULL;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
    git_check(git_status_list_new(&list, repo, &opts), "status");

    size_t n = git_status_list_entrycount(list);
    curry_val result = curry_nil();
    for (size_t i = 0; i < n; i++) {
        const git_status_entry *e = git_status_byindex(list, i);
        const char *path = e->index_to_workdir ? e->index_to_workdir->old_file.path
                                               : e->head_to_index->old_file.path;

        /* Encode flags as a list of symbols */
        curry_val flags = curry_nil();
        if (e->status & GIT_STATUS_INDEX_NEW)       flags = curry_make_pair(curry_make_symbol("staged-new"),     flags);
        if (e->status & GIT_STATUS_INDEX_MODIFIED)  flags = curry_make_pair(curry_make_symbol("staged-modified"),flags);
        if (e->status & GIT_STATUS_INDEX_DELETED)   flags = curry_make_pair(curry_make_symbol("staged-deleted"), flags);
        if (e->status & GIT_STATUS_WT_NEW)          flags = curry_make_pair(curry_make_symbol("untracked"),      flags);
        if (e->status & GIT_STATUS_WT_MODIFIED)     flags = curry_make_pair(curry_make_symbol("modified"),       flags);
        if (e->status & GIT_STATUS_WT_DELETED)      flags = curry_make_pair(curry_make_symbol("deleted"),        flags);
        if (e->status & GIT_STATUS_CONFLICTED)      flags = curry_make_pair(curry_make_symbol("conflicted"),     flags);

        curry_val entry = curry_make_pair(curry_make_string(path), flags);
        result = curry_make_pair(entry, result);
    }
    git_status_list_free(list);
    return result;
}

static curry_val fn_head(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_reference *ref = NULL;
    if (git_repository_head(&ref, repo) < 0) {
        return curry_make_string("(no HEAD)");
    }
    const char *name = git_reference_shorthand(ref);
    curry_val r = curry_make_string(name);
    git_reference_free(ref);
    return r;
}

/* ---- Log ---- */

static curry_val commit_to_alist(git_commit *c) {
    char oid_str[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(oid_str, sizeof(oid_str), git_commit_id(c));

    const git_signature *author = git_commit_author(c);
    const char *msg = git_commit_message(c);
    /* Strip trailing newline from message */
    char msg_buf[1024];
    strncpy(msg_buf, msg ? msg : "", sizeof(msg_buf)-1);
    msg_buf[sizeof(msg_buf)-1] = '\0';
    size_t mlen = strlen(msg_buf);
    while (mlen > 0 && (msg_buf[mlen-1] == '\n' || msg_buf[mlen-1] == '\r'))
        msg_buf[--mlen] = '\0';

    curry_val alist = curry_nil();
    alist = curry_make_pair(curry_make_pair(curry_make_symbol("time"),
                            curry_make_fixnum((intptr_t)(author ? author->when.time : 0))), alist);
    alist = curry_make_pair(curry_make_pair(curry_make_symbol("email"),
                            curry_make_string(author ? author->email : "")), alist);
    alist = curry_make_pair(curry_make_pair(curry_make_symbol("author"),
                            curry_make_string(author ? author->name : "")), alist);
    alist = curry_make_pair(curry_make_pair(curry_make_symbol("message"),
                            curry_make_string(msg_buf)), alist);
    alist = curry_make_pair(curry_make_pair(curry_make_symbol("id"),
                            curry_make_string(oid_str)), alist);
    return alist;
}

static curry_val fn_log(int ac, curry_val *av, void *ud) {
    (void)ud;
    git_repository *repo = val_to_repo(av[0]);
    int limit = (ac >= 2) ? (int)curry_fixnum(av[1]) : 100;

    git_revwalk *walk = NULL;
    git_check(git_revwalk_new(&walk, repo), "log");
    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_revwalk_push_head(walk);

    curry_val result = curry_nil();
    git_oid oid;
    int count = 0;
    while (count < limit && git_revwalk_next(&oid, walk) == 0) {
        git_commit *c = NULL;
        if (git_commit_lookup(&c, repo, &oid) == 0) {
            result = curry_make_pair(commit_to_alist(c), result);
            git_commit_free(c);
        }
        count++;
    }
    git_revwalk_free(walk);

    /* Reverse to chronological (most recent first was already reversed) */
    curry_val rev = curry_nil();
    while (curry_is_pair(result)) {
        rev = curry_make_pair(curry_car(result), rev);
        result = curry_cdr(result);
    }
    return rev;
}

/* ---- Index / staging ---- */

static curry_val fn_add(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_index *idx = NULL;
    git_check(git_repository_index(&idx, repo), "index");
    git_check(git_index_add_bypath(idx, curry_string(av[1])), "add");
    git_check(git_index_write(idx), "index write");
    git_index_free(idx);
    return curry_void();
}

static curry_val fn_add_all(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_index *idx = NULL;
    git_check(git_repository_index(&idx, repo), "index");
    git_strarray pathspec = {NULL, 0};
    git_check(git_index_add_all(idx, &pathspec, GIT_INDEX_ADD_DEFAULT, NULL, NULL), "add-all");
    git_check(git_index_write(idx), "index write");
    git_index_free(idx);
    return curry_void();
}

static curry_val fn_reset_file(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_reference *head_ref = NULL;
    if (git_repository_head(&head_ref, repo) < 0) {
        /* No HEAD yet (empty repo) — just remove from index */
        git_index *idx = NULL;
        git_check(git_repository_index(&idx, repo), "index");
        git_check(git_index_remove_bypath(idx, curry_string(av[1])), "reset");
        git_check(git_index_write(idx), "index write");
        git_index_free(idx);
        return curry_void();
    }
    git_object *head_obj = NULL;
    git_check(git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT), "peel");
    git_strarray pathspec;
    char *paths[1] = { (char *)curry_string(av[1]) };
    pathspec.strings = paths; pathspec.count = 1;
    git_check(git_reset_default(repo, head_obj, &pathspec), "reset");
    git_object_free(head_obj);
    git_reference_free(head_ref);
    return curry_void();
}

/* ---- Commit ---- */

static curry_val fn_commit(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo  = val_to_repo(av[0]);
    const char *msg       = curry_string(av[1]);
    const char *auth_name = curry_string(av[2]);
    const char *auth_mail = curry_string(av[3]);

    git_index *idx = NULL;
    git_check(git_repository_index(&idx, repo), "commit index");

    git_oid tree_oid;
    git_check(git_index_write_tree(&tree_oid, idx), "write tree");
    git_index_free(idx);

    git_tree *tree = NULL;
    git_check(git_tree_lookup(&tree, repo, &tree_oid), "tree lookup");

    git_signature *sig = NULL;
    git_check(git_signature_now(&sig, auth_name, auth_mail), "signature");

    /* Parent: current HEAD, if any */
    git_commit *parent = NULL;
    git_reference *head_ref = NULL;
    if (git_repository_head(&head_ref, repo) == 0) {
        git_object *head_obj = NULL;
        git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT);
        git_commit_lookup(&parent, repo, git_object_id(head_obj));
        git_object_free(head_obj);
        git_reference_free(head_ref);
    }

    const git_commit *parents[1] = { parent };
    int parent_count = parent ? 1 : 0;

    git_oid commit_oid;
    git_check(git_commit_create(&commit_oid, repo, "HEAD", sig, sig,
                                "UTF-8", msg, tree,
                                (size_t)parent_count, parents), "commit create");

    char oid_str[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(oid_str, sizeof(oid_str), &commit_oid);

    git_signature_free(sig);
    git_tree_free(tree);
    if (parent) git_commit_free(parent);

    return curry_make_string(oid_str);
}

/* ---- Branches ---- */

static curry_val fn_branches(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_branch_iterator *it = NULL;
    git_check(git_branch_iterator_new(&it, repo, GIT_BRANCH_LOCAL), "branches");

    curry_val result = curry_nil();
    git_reference *ref = NULL;
    git_branch_t btype;
    while (git_branch_next(&ref, &btype, it) == 0) {
        const char *name = NULL;
        git_branch_name(&name, ref);
        result = curry_make_pair(curry_make_string(name ? name : ""), result);
        git_reference_free(ref);
    }
    git_branch_iterator_free(it);
    return result;
}

static curry_val fn_current_branch(int ac, curry_val *av, void *ud) {
    return fn_head(ac, av, ud);
}

static curry_val fn_checkout(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    const char *branch   = curry_string(av[1]);

    git_reference *ref = NULL;
    char refname[256];
    snprintf(refname, sizeof(refname), "refs/heads/%s", branch);
    git_check(git_reference_lookup(&ref, repo, refname), "checkout lookup");

    git_object *obj = NULL;
    git_check(git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT), "checkout peel");

    git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
    copts.checkout_strategy = GIT_CHECKOUT_SAFE;
    git_check(git_checkout_tree(repo, obj, &copts), "checkout tree");
    git_check(git_repository_set_head(repo, refname), "set HEAD");

    git_object_free(obj);
    git_reference_free(ref);
    return curry_void();
}

static curry_val fn_branch_create(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    const char *name     = curry_string(av[1]);

    git_reference *head_ref = NULL;
    git_check(git_repository_head(&head_ref, repo), "branch-create head");
    git_object *head_obj = NULL;
    git_check(git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT), "branch-create peel");
    git_commit *head_commit = NULL;
    git_check(git_commit_lookup(&head_commit, repo, git_object_id(head_obj)), "branch-create commit");

    git_reference *new_ref = NULL;
    git_check(git_branch_create(&new_ref, repo, name, head_commit, 0), "branch create");

    git_reference_free(new_ref);
    git_commit_free(head_commit);
    git_object_free(head_obj);
    git_reference_free(head_ref);
    return curry_void();
}

/* ---- Diff ---- */

static int diff_cb(const git_diff_delta *d, const git_diff_hunk *h,
                   const git_diff_line *line, void *payload) {
    (void)d; (void)h;
    char **out = (char **)payload;
    if (line->origin == GIT_DIFF_LINE_CONTEXT  ||
        line->origin == GIT_DIFF_LINE_ADDITION ||
        line->origin == GIT_DIFF_LINE_DELETION) {
        size_t cur_len = *out ? strlen(*out) : 0;
        *out = realloc(*out, cur_len + line->content_len + 2);
        (*out)[cur_len] = line->origin;
        memcpy(*out + cur_len + 1, line->content, line->content_len);
        (*out)[cur_len + 1 + line->content_len] = '\0';
    }
    if (line->origin == GIT_DIFF_LINE_FILE_HDR) {
        size_t cur_len = *out ? strlen(*out) : 0;
        size_t path_len = strlen(d->new_file.path);
        *out = realloc(*out, cur_len + path_len + 16);
        snprintf(*out + cur_len, path_len + 16, "--- %s\n", d->new_file.path);
    }
    return 0;
}

static curry_val fn_diff(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_diff *diff = NULL;
    git_diff_index_to_workdir(&diff, repo, NULL, NULL);

    char *out = NULL;
    if (diff) {
        git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, diff_cb, &out);
        git_diff_free(diff);
    }
    curry_val result = curry_make_string(out ? out : "");
    free(out);
    return result;
}

static curry_val fn_diff_staged(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_diff *diff = NULL;

    git_object *head_obj = NULL;
    git_reference *head_ref = NULL;
    if (git_repository_head(&head_ref, repo) == 0) {
        git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT);
        git_tree *head_tree = NULL;
        git_commit *c = NULL;
        git_commit_lookup(&c, repo, git_object_id(head_obj));
        if (c) { git_commit_tree(&head_tree, c); git_commit_free(c); }
        git_diff_index_to_workdir(&diff, repo, NULL, NULL);  /* placeholder */
        git_diff_free(diff); diff = NULL;
        git_diff_tree_to_index(&diff, repo, head_tree, NULL, NULL);
        if (head_tree) git_tree_free(head_tree);
        git_object_free(head_obj);
        git_reference_free(head_ref);
    } else {
        git_diff_index_to_workdir(&diff, repo, NULL, NULL);
    }

    char *out = NULL;
    if (diff) {
        git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, diff_cb, &out);
        git_diff_free(diff);
    }
    curry_val result = curry_make_string(out ? out : "");
    free(out);
    return result;
}

/* ---- Tags ---- */

static int tag_cb(const char *name, git_oid *oid, void *payload) {
    (void)oid;
    curry_val *list = (curry_val *)payload;
    *list = curry_make_pair(curry_make_string(name), *list);
    return 0;
}

static curry_val fn_tags(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    curry_val result = curry_nil();
    git_tag_foreach(repo, tag_cb, &result);
    return result;
}

static curry_val fn_tag_create(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    const char *name     = curry_string(av[1]);
    const char *msg      = curry_string(av[2]);

    git_reference *head_ref = NULL;
    git_check(git_repository_head(&head_ref, repo), "tag head");
    git_object *head_obj = NULL;
    git_check(git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT), "tag peel");
    git_signature *sig = NULL;
    git_check(git_signature_now(&sig, "curry", "curry@localhost"), "tag sig");

    git_oid tag_oid;
    git_check(git_tag_create(&tag_oid, repo, name, head_obj, sig, msg, 0), "tag create");

    git_signature_free(sig);
    git_object_free(head_obj);
    git_reference_free(head_ref);
    return curry_void();
}

/* ---- Remotes ---- */

static curry_val fn_remotes(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo = val_to_repo(av[0]);
    git_strarray names = {0};
    git_check(git_remote_list(&names, repo), "remotes");
    curry_val result = curry_nil();
    for (size_t i = 0; i < names.count; i++) {
        git_remote *remote = NULL;
        if (git_remote_lookup(&remote, repo, names.strings[i]) == 0) {
            const char *url = git_remote_url(remote);
            result = curry_make_pair(
                curry_make_pair(curry_make_string(names.strings[i]),
                                curry_make_string(url ? url : "")),
                result);
            git_remote_free(remote);
        }
    }
    git_strarray_dispose(&names);
    return result;
}

static curry_val fn_fetch(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo   = val_to_repo(av[0]);
    const char *remote_name= curry_string(av[1]);
    git_remote *remote = NULL;
    git_check(git_remote_lookup(&remote, repo, remote_name), "fetch lookup");
    git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
    git_check(git_remote_fetch(remote, NULL, &opts, NULL), "fetch");
    git_remote_free(remote);
    return curry_void();
}

static curry_val fn_push(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    git_repository *repo   = val_to_repo(av[0]);
    const char *remote_name= curry_string(av[1]);
    const char *branch     = curry_string(av[2]);
    git_remote *remote = NULL;
    git_check(git_remote_lookup(&remote, repo, remote_name), "push lookup");
    char refspec_buf[256];
    snprintf(refspec_buf, sizeof(refspec_buf), "refs/heads/%s:refs/heads/%s", branch, branch);
    char *refspecs[1] = { refspec_buf };
    git_strarray refs = { refspecs, 1 };
    git_push_options opts = GIT_PUSH_OPTIONS_INIT;
    git_check(git_remote_push(remote, &refs, &opts), "push");
    git_remote_free(remote);
    return curry_void();
}

/* ---- Module entry point ---- */

void curry_module_init(CurryVM *vm) {
    git_libgit2_init();

    /* Repository */
    curry_define_fn(vm, "git-open",         fn_open,         1, 1, NULL);
    curry_define_fn(vm, "git-init",         fn_init,         1, 1, NULL);
    curry_define_fn(vm, "git-clone",        fn_clone,        2, 2, NULL);
    curry_define_fn(vm, "git-close!",       fn_close,        1, 1, NULL);

    /* Status */
    curry_define_fn(vm, "git-status",       fn_status,       1, 1, NULL);
    curry_define_fn(vm, "git-head",         fn_head,         1, 1, NULL);

    /* Log */
    curry_define_fn(vm, "git-log",          fn_log,          1, 2, NULL);

    /* Index */
    curry_define_fn(vm, "git-add!",         fn_add,          2, 2, NULL);
    curry_define_fn(vm, "git-add-all!",     fn_add_all,      1, 1, NULL);
    curry_define_fn(vm, "git-reset-file!",  fn_reset_file,   2, 2, NULL);

    /* Commit */
    curry_define_fn(vm, "git-commit!",      fn_commit,       4, 4, NULL);

    /* Branches */
    curry_define_fn(vm, "git-branches",     fn_branches,     1, 1, NULL);
    curry_define_fn(vm, "git-current-branch",fn_current_branch,1,1,NULL);
    curry_define_fn(vm, "git-checkout!",    fn_checkout,     2, 2, NULL);
    curry_define_fn(vm, "git-branch-create!",fn_branch_create,2,2,NULL);

    /* Diff */
    curry_define_fn(vm, "git-diff",         fn_diff,         1, 1, NULL);
    curry_define_fn(vm, "git-diff-staged",  fn_diff_staged,  1, 1, NULL);

    /* Tags */
    curry_define_fn(vm, "git-tags",         fn_tags,         1, 1, NULL);
    curry_define_fn(vm, "git-tag-create!",  fn_tag_create,   3, 3, NULL);

    /* Remote */
    curry_define_fn(vm, "git-remotes",      fn_remotes,      1, 1, NULL);
    curry_define_fn(vm, "git-fetch!",       fn_fetch,        2, 2, NULL);
    curry_define_fn(vm, "git-push!",        fn_push,         3, 3, NULL);
}
