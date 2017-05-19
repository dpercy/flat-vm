(program


(define (plus_vec2 a b)
  (let1 (struct ax ay) a
  (let1 (struct bx by) b
  (return (struct (+ ax bx) (+ ay by))))))

(plus_vec2 (plus_vec2 (struct 1 2) (struct 10 100))
           (struct 4 8))


)
