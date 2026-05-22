#!/usr/bin/env curry
;;; Blackjack (21) - terminal, single deck, basic rules

(define suits '("S" "H" "D" "C"))
(define ranks '("A" "2" "3" "4" "5" "6" "7" "8" "9" "10" "J" "Q" "K"))

(define (make-deck)
  (apply append
    (map (lambda (suit)
           (map (lambda (rank) (cons rank suit)) ranks))
         suits)))

(define (urandom-byte)
  (let* ((p (open-input-file "/dev/urandom"))
         (b (read-u8 p)))
    (close-port p)
    b))

(define (shuffle! deck)
  (let ((v (list->vector deck)))
    (let loop ((i (- (vector-length v) 1)))
      (when (> i 0)
        (let* ((j (remainder (urandom-byte) (+ i 1)))
               (tmp (vector-ref v i)))
          (vector-set! v i (vector-ref v j))
          (vector-set! v j tmp)
          (loop (- i 1)))))
    (vector->list v)))

(define (hand-value hand)
  (let loop ((cards hand) (total 0) (aces 0))
    (if (null? cards)
        (let adjust ((t total) (a aces))
          (if (and (> t 21) (> a 0))
              (adjust (- t 10) (- a 1))
              t))
        (let ((r (car (car cards))))
          (cond
            ((member r '("J" "Q" "K")) (loop (cdr cards) (+ total 10) aces))
            ((equal? r "A")            (loop (cdr cards) (+ total 11) (+ aces 1)))
            (else                      (loop (cdr cards) (+ total (string->number r)) aces)))))))

(define (card->str card)
  (string-append (car card) (cdr card)))

(define (show-hand label hand hide-second)
  (display label)
  (display ": ")
  (let loop ((cards hand) (n 0))
    (unless (null? cards)
      (if (and hide-second (= n 1))
          (display "[??]")
          (display (card->str (car cards))))
      (unless (null? (cdr cards)) (display " "))
      (loop (cdr cards) (+ n 1))))
  (unless hide-second
    (display " = ")
    (display (hand-value hand)))
  (newline))

(define (bust? hand)
  (> (hand-value hand) 21))

(define (blackjack? hand)
  (and (= (length hand) 2) (= (hand-value hand) 21)))

(define (prompt msg)
  (display msg)
  (flush-output-port (current-output-port))
  (let ((line (read-line)))
    (if (eof-object? line) "" line)))

(define (dealer-play dh shoe player-val chips bet)
  (display "\n-- Dealer reveals --\n")
  (show-hand "Dealer" dh #f)
  (let loop ((dh dh) (shoe shoe))
    (cond
      ((bust? dh)
       (display "Dealer busts! You win!\n")
       (+ chips bet))
      ((>= (hand-value dh) 17)
       (let ((dv (hand-value dh)))
         (cond
           ((> player-val dv) (display "You win!\n")    (+ chips bet))
           ((< player-val dv) (display "Dealer wins.\n") (- chips bet))
           (else              (display "Push.\n")        chips))))
      (else
       (let ((new-dh (cons (car shoe) dh)))
         (show-hand "Dealer" new-dh #f)
         (loop new-dh (cdr shoe)))))))

(define (player-play hand shoe dh chips bet)
  (cond
    ((bust? hand)
     (display "Bust! You lose.\n")
     (- chips bet))
    ((= (hand-value hand) 21)
     (display "21!\n")
     (dealer-play dh shoe (hand-value hand) chips bet))
    (else
     (let ((choice (prompt "  [h]it / [s]tand / [d]ouble? ")))
       (cond
         ((equal? choice "d")
          (if (> (* 2 bet) chips)
              (begin (display "Not enough chips to double.\n")
                     (player-play hand shoe dh chips bet))
              (let ((new-hand (cons (car shoe) hand)))
                (show-hand "You   " new-hand #f)
                (if (bust? new-hand)
                    (begin (display "Bust on double! You lose double.\n")
                           (- chips (* 2 bet)))
                    (dealer-play dh (cdr shoe) (hand-value new-hand) chips (* 2 bet))))))
         ((or (equal? choice "h") (equal? choice "hit"))
          (let ((new-hand (cons (car shoe) hand)))
            (show-hand "You   " new-hand #f)
            (player-play new-hand (cdr shoe) dh chips bet)))
         (else
          (dealer-play dh shoe (hand-value hand) chips bet)))))))

(define (play-round chips)
  (let* ((deck     (shuffle! (make-deck)))
         (c1 (car deck)) (c2 (cadr deck))
         (c3 (caddr deck)) (c4 (car (cdddr deck)))
         (shoe     (cdr (cdddr deck)))
         (player   (list c1 c3))
         (dealer   (list c2 c4))
         (bet-str  (prompt (string-append "\nChips: " (number->string chips) "  Bet: ")))
         (bet      (string->number bet-str)))
    (if (or (not bet) (<= bet 0) (> bet chips))
        (begin (display "Invalid bet.\n") (play-round chips))
        (begin
          (newline)
          (show-hand "Dealer" dealer #t)
          (show-hand "You   " player #f)
          (cond
            ((blackjack? player)
             (display "Blackjack! You win 3:2!\n")
             (+ chips (inexact->exact (round (* bet 3/2)))))
            (else
             (player-play player shoe dealer chips bet)))))))

(define (game)
  (display "=== BLACKJACK ===\n")
  (display "Commands: h=hit  s=stand  d=double\n")
  (let loop ((chips 100))
    (if (<= chips 0)
        (display "Out of chips. Game over!\n")
        (let ((new-chips (play-round chips)))
          (let ((again (prompt "Play again? [y/n] ")))
            (if (equal? again "y")
                (loop new-chips)
                (begin
                  (display "Final chips: ")
                  (display new-chips)
                  (newline))))))))

(game)
