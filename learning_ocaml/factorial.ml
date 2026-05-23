let rec factorial n =
    if n <= 1 then
        1
    else
        n * factorial (n-1)

let () =
    Printf.printf "Factorial of 50 is %d\n" (factorial 50)
