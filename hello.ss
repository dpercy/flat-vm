(program

(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1))
         (fib (- n 2)))))

(fib 8)
; 0 1 2 3 4 5 6  7  8
; 0 1 1 2 3 5 8 13 21

)
