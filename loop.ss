(program

(define (triangle n)
  ; find the nth triangle number:
  ; 1, (1 + 2), (1 + 2 + 3), etc
  ; for i in inclusive_range(1, n): sum += i
  (labels ([loop (i sum)
                (if (> i n)
                    (return sum)
                    (loop (+ i 1) (+ sum i)))])
    (loop 1 0)))

(triangle 100000000)
                    
    


)
