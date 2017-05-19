#lang racket


#|

program := expr
| (program funcdef ... expr)


funcdef :=
| (define (funcname var ...) expr)


; TODO support floats. problem is VM arithmetic is monomorphic.
expr :=
| integer
| (op expr ...)
| (struct expr ...)
| (let1 var expr expr)
| (let1 (struct var ...) expr expr)
| (funcname expr ...)
| (if expr expr expr)

op := + - * / % < <= > >= == !=


|#

(define keywords
  '(
    program
    define
    struct
    let1
    if
    ))
(define binops '(+ - * / % < <= > >= == !=))
(define (binop? v) (member v binops))
(define (keyword? v) (member v keywords))
(define (iden? v) (and (symbol? v) (not (binop? v)) (not (keyword? v))))

(define (line . args)
  (apply string-append (append (map ~a args))))
(module+ test
  (require rackunit)
  (check-equal? (line 1 2 3) "123")
  (check-equal? (line "foo(" 1.2 ");") "foo(1.2);"))
(define (lines . args)
  (apply string-append (add-between (flatten args) "\n")))
(module+ test
  (check-equal? (lines "a" "b" "c") "a\nb\nc")
  (check-equal? (lines "0" "1" '("2" "3")) "0\n1\n2\n3")
  (check-equal? (lines "1" '("2" "3")) "1\n2\n3")
  (check-equal? (lines '("2" "3")) "2\n3"))


(define ENV (make-parameter '())) ; list of symbols
(define (compile-expr sexpr) ; -> string
  (match sexpr
    [(? fixnum? n) (line "push_i32(" n ");")]
    [(? symbol? x) (or (for/first ([i (in-naturals)]
                                   [e (in-list (ENV))]
                                   #:when (equal? x e))
                         (line "grab(" i ");")
                         )
                       (error 'compile-expr "unbound variable: ~v in ENV: ~v" x (ENV)))]
    [`(,(? binop? op) ,x ,y) (lines (compile-exprs (list x y))
                                    (line (opname op) "_i32();"))]
    [`(struct ,args ...) (lines (compile-exprs args)
                                (line "construct(" (length args) ");"))]
    [`(let1 ,(? iden? lhs) ,rhs ,body)
     (lines (compile-expr rhs)
            (parameterize ([ENV (cons lhs (ENV))])
              (compile-expr body))
            (line "cut(1, 1);")
            )]
    [`(let1 (struct ,(? iden? lhses) ...) ,rhs ,body)
     (lines (compile-expr rhs)
            (line "destruct(" (length lhses) ");")
            ; rightmost binds are innermost / top of env stack,
            ; because (let1 (struct a b) (struct 1 2) body) == (let1 a 1 (let1 b 2 body))
            (parameterize ([ENV (append (reverse lhses) (ENV))])
              (compile-expr body))
            ; pop all the locals back off
            (line "cut(1, " (length lhses) ");"))]

    [`(if ,test ,consq ,alt) (lines (compile-expr test)
                                    "if (pop_i32()) {"
                                    (compile-expr consq)
                                    "} else {"
                                    (compile-expr alt)
                                    "}")]
    [`(,(? iden? callee) ,args ...) (lines (compile-expr `(struct ,@args))
                                           ; all functions take a single struct
                                           (line callee "();"))]

    ; TODO jumps / ghc join points / local continuations
    ))
(define (compile-exprs sexprs)
  (match sexprs
    ['() ""]
    [(cons hd tl) (lines (compile-expr hd)
                         (parameterize ([ENV (cons "tmp" (ENV))])
                           (compile-exprs tl)))]))

(define (compile-program sexpr)
  (match sexpr
    [`(program ,funcdefs ... ,expr)
     (lines (map compile-funcdef funcdefs)
            "int main() {"
            (compile-expr expr)
            "}")]))
(define (compile-funcdef sexpr)
  (match sexpr
    [`(define (,name ,params ...) ,body)
     (lines (line "void " name "() {")
            (line "debug_print(" (~s (format "enter ~a" name)) ");")
            (line "destruct(" (length params) ");")
            (parameterize ([ENV (append (reverse params) (ENV))])
              (compile-expr body))
            (line "cut(1, " (length params) ");")
            (line "debug_print(" (~s (format "exit ~a" name)) ");")
            (line "}"))]))

(define compile "DO NOT CALL THIS")

(define (opname op)
  (match op
    ['+ 'add]
    ['- 'sub]
    ['* 'mul]
    ['/ 'div]
    ['% 'mod]
    ['< 'lt]
    ['> 'gt]
    ['<= 'lte]
    ['>= 'gte]
    ['== 'eq]
    ['!= 'neq]))

(module+ main
  (displayln

   (lines "#include \"vm.c\""
          (compile-program (read)))))
