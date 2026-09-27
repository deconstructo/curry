(define-library (srfi srfi-189)
  (import (srfi s189 maybe-either))
  (export
    just nothing right left
    just? nothing? right? left? maybe? either?
    just-payload right-payload
    maybe= either=
    list->just list->left list->right
    maybe->either either->maybe either-swap
    maybe-ref either-ref maybe-ref/default either-ref/default
    maybe-join either-join maybe-compose either-compose maybe-bind either-bind
    maybe-length either-length
    maybe-filter maybe-remove either-filter either-remove
    maybe-sequence either-sequence
    maybe->list either->list list->maybe list->either
    maybe->truth either->truth truth->maybe truth->either
    maybe->list-truth either->list-truth list-truth->maybe list-truth->either
    maybe->generation either->generation generation->maybe generation->either
    maybe->values either->values values->maybe values->either
    maybe->two-values two-values->maybe
    exception->either
    maybe-map either-map maybe-for-each either-for-each
    maybe-fold either-fold maybe-unfold either-unfold
    maybe-if maybe-and either-and maybe-or either-or
    maybe-let* either-let* maybe-let*-values either-let*-values
    either-guard
    tri-not tri=? tri-and tri-or tri-merge))
