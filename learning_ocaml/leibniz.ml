(*
Leibniz formula for calculating pi:
pi = 4 - 4/3 + 4/5 - 4/7 + 4/9...
*)

let rec approx_pi_floats i iterations acc = 
    if i >= iterations then
        acc
    else
        let sign = match (i mod 2) with | 1 -> -1. | _ -> 1. in
        let next_acc = acc +. sign *. 4. /. ((float_of_int i)*.2. +. 1.) in
        approx_pi_floats (i+1) iterations next_acc

let rec approx_pi_ints i iterations (acc_num, acc_den) = 
    if i >= iterations then
        (acc_num, acc_den)
    else
        let sign = match (i mod 2) with | 1 -> -1 | _ -> 1 in
        let l_den = i*2 + 1 in
        let next_acc_num = sign * acc_num in
        let next_acc_den = acc_den * l_den in
        approx_pi_ints (i+1) iterations (next_acc_num, next_acc_den)

let () = 
    (* Printf.printf "%f\n" (approx_pi_floats 0 200000000 0.) *)
    let num, den = approx_pi_ints 1 10 (0, 1) in 
    Printf.printf "%d/%d\n" num den





let add_fractions (num1, den1) (num2, den2) =
    let new_num = (num1 * den2) + (num2 * den1) in
    let new_den = den1 * den2 in
    (new_num, new_den)

(* Calculates Pi using Leibniz formula up to a max number of iterations *)
let calculate_pi_fraction iterations =
    (* Tail-recursive loop accumulating our fraction (num, den) *)
    let rec loop i (num, den) sign =
        if i >= iterations
        then (num, den)
        else
            let term_den = (2 * i) + 1 in
            let term_num = if sign then 4 else -4 in

            (* Add the current term: (num/den) + (term_num/term_den) *)
            let next_fraction = add_fractions (num, den) (term_num, term_den) in

            (* Flip the sign flag for the next term *)
            loop (i + 1) next_fraction (not sign)
    in
    (* Start with 0/1, iteration 0, positive sign *)
    loop 0 (0, 1) true

let () =
    (* 12 iterations is about the maximum safe limit before 64-bit integer overflow! *)
    let iterations = 12 in

    let num, den = calculate_pi_fraction iterations in

    (* Convert the final exact fraction to a float just to see the decimal representation *)
    let decimal_value = float_of_int num /. float_of_int den in

    Printf.printf "Leibniz Pi approximation using exact fractions (%d iterations):\n\n" iterations;
    Printf.printf "Exact Fraction = %d / %d\n" num den;
    Printf.printf "Decimal Value  = %.10f\n" decimal_value;
    Printf.printf "Actual Pi      = 3.1415926535...\n"