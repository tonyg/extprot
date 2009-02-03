(define counter 0)

(define (pad-with-zeros n)
  (let ((s (string-append "00000" (number->string n))))
    (substring s (- (string-length s) 5) (string-length s))))

(define (check_write s)
  (with-output-to-file (string-append "test_data_"(pad-with-zeros counter)".extprot")
    (lambda ()
      (let loop ((s (string->list s)))
	(if (null? s)
	    'done
	    (begin
	      (write-byte (foldl (lambda (n acc) (+ (* acc 10) n))
				 0
				 (map (lambda (c) (- (char->integer c) 48)) (cdr (take s 4)))))
	      (loop (drop s 4)))))))
  (set! counter (+ counter 1)))

(check_write " 001 008 001 001 005 002 000 020 002 001")
(check_write " 001 003 001 002 000")
(check_write " 017 003 001 000 020")
(check_write " 001 006 001 001 003 001 002 001")
(check_write " 001 006 001 017 003 001 002 128")
(check_write " 001 010 001 033 007 001 003 004 097 098 099 100")
(check_write " 001 002 001 010")
(check_write " 001 015 002 001 010 001 033 007 001 003 004 097 098 099 100 000 020")
(check_write " 001 018 002 005 006 002 000 020 000 128 004 005 007 003 002 001 002 000 002 000")
(check_write " 001 003 001 002 001")
(check_write " 001 003 001 002 000")
(check_write " 001 003 001 002 065")
(check_write " 001 018 001 007 015 002 003 004 097 098 099 100 000 000 003 002 101 102 000 001")
