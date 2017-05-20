#lang racket


#|

program := expr
| (program funcdef ... expr)


funcdef :=
| (define (funcname var ...) expr)

stmt :=
| (return expr)
| (if expr stmt stmt)
| (let1 var expr stmt)
| (let1 (struct var ...) expr stmt)
| (labels ([lblname (var ...) stmt] ...) stmt)
| (lblname expr ...)

expr :=
| integer
| (op expr ...)
| (struct expr ...)
| (funcname expr ...)

op := + - * / % < <= > >= == !=


|#

(define keywords
  '(
    program
    define
    return
    struct
    if
    let1
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
  (apply string-append (add-between (remove* (list "") (flatten args))
                                    "\n")))
(module+ test
  (check-equal? (lines "a" "b" "c") "a\nb\nc")
  (check-equal? (lines "0" "1" '("2" "3")) "0\n1\n2\n3")
  (check-equal? (lines "1" '("2" "3")) "1\n2\n3")
  (check-equal? (lines '("2" "3")) "2\n3"))

; an env is either:
; '()
; (cons var env)
; (cons "tmp" env)
; (cons (hash lblname -> params) env)

(define ENV (make-parameter '()))
(define (env-variable-index env x)
  (or (for/first ([i (in-naturals)]
                  [e (in-list (filter (or/c symbol? "tmp") env))]
                  #:when (equal? x e))
        i)
      (error 'compile-expr "unbound variable: ~v in ENV: ~v" x (ENV))))
(define (env-push-temp env)
  (cons "tmp" env))
(define (env-push-vars env vars)
  (append (reverse vars) env))
(define (env-return-cutcount env)
  ; valid because functions cannot be nested:
  ; the number of params and local we need to cut is simply the size of the ENV.
  (length (filter (or/c symbol? "tmp") env)))
(define (env-push-labels env defs)
  (cons (for/hash ([def defs])
          (match def
            [(list name params _) (values name params)]))
        env))
(define (env-label-argcount env lbl)
  (or (for/first ([frame (in-list env)]
                  #:when (hash? frame)
                  #:when (hash-has-key? frame lbl))
        (length (hash-ref frame lbl)))
      (error 'compile
             "undefined label ~v in ENV: ~v"
             lbl env)))
(define (env-label-cutcount env lbl)
  (for/sum ([frame (in-list env)]
            #:break (and (hash? frame)
                         (hash-has-key? frame lbl))
            #:when ((or/c symbol? "tmp") frame))
    1))



(define (compile-expr sexpr) ; -> string
  (match sexpr
    [(? fixnum? n) (line "push_i32(" n ");")]
    [(? symbol? x) (line "grab(" (env-variable-index (ENV) x) ");")]
    [`(,(? binop? op) ,x ,y) (lines (compile-exprs (list x y))
                                    (line (opname op) "_i32();"))]
    [`(struct ,args ...) (lines (compile-exprs args)
                                (line "construct(" (length args) ");"))]
    [`(,(? iden? callee) ,args ...)
     ; all functions take a single struct
     (lines (compile-expr `(struct ,@args))
            (line "debug_print(" (~s (format "exit ~a" callee)) ");")
            (line callee "();")
            (line "debug_print(" (~s (format "exit ~a" callee)) ");"))]))
(define (compile-exprs sexprs)
  (match sexprs
    ['() ""]
    [(cons hd tl) (lines (compile-expr hd)
                         (parameterize ([ENV (env-push-temp (ENV))])
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
            ; unpack the single arguments-struct
            (line "destruct(" (length params) ");")
            ; env needs to track
            ; 1. for each variable name, its offset
            ; 2. how many variables to pop when doing a return
            ; 3. soon: how many variables to pop when jumping to some label
            (parameterize* ([ENV (env-push-vars (ENV) params)])
              (compile-stmt body))
            (line "}"))]))

(define (compile-stmt sexpr)
  (match sexpr
    [`(return ,v) (lines (compile-expr v)
                         (line "cut(1, " (env-return-cutcount (ENV)) ");")
                         (line "return;"))]
    [`(if ,test ,consq ,alt) (lines (compile-expr test)
                                    "if (pop_i32()) {"
                                    (compile-stmt consq)
                                    "} else {"
                                    (compile-stmt alt)
                                    "}")]
    [`(let1 ,(? iden? lhs) ,rhs ,body)
     (lines (compile-expr rhs)
            (parameterize ([ENV (env-push-vars (ENV) (list lhs))])
              (compile-stmt body)))]
    [`(let1 (struct ,(? iden? lhses) ...) ,rhs ,body)
     (lines (compile-expr rhs)
            (line "destruct(" (length lhses) ");")
            ; rightmost binds are innermost / top of env stack,
            ; because (let1 (struct a b) (struct 1 2) body) == (let1 a 1 (let1 b 2 body))
            (parameterize ([ENV (env-push-vars (ENV) lhses)])
              (compile-stmt body)))]
    [`(labels ,defs ,body)
     (parameterize ([ENV (env-push-labels (ENV) defs)])
       ; emit code for the body first, so it will be executed first.
       ; the labels can come afterwards in the text,
       ; and will only be reachable by an explicit goto.
       (lines (compile-stmt body)
              (map compile-lbldef defs)))]
    [`(,(? iden? lbl) ,args ...)
     (unless (= (length args)
                (env-label-argcount (ENV) lbl))
       (error 'compile "label ~v requires ~v arguments, but got ~v"
              lbl (env-label-argcount (ENV) lbl) args))
     (lines (compile-exprs args)
            (line "cut("
                  ; how many values do we pass to the label?
                  (length args)
                  ", "
                  ; how many things do we need to cut to get back
                  ; to the label's defining scope?
                  (env-label-cutcount (ENV) lbl)
                  ");")
            (line "goto " lbl ";"))]))
(define (compile-lbldef sexpr)
  (match sexpr
    [(list name params body)
     (lines (line name ":")
            (parameterize ([ENV (env-push-vars (ENV) params)])
              (compile-stmt body)))]))

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
