open Z
  
let rec factorial_jay n = 
    if n = (Z.of_int 1) then
        (Z.of_int 1)
    else
        factorial_jay (n-(Z.of_int 1)) * n


let integer_sqrt target =
  if Z.lt target Z.zero then invalid_arg "negative number"
  else if Z.equal target Z.zero then Z.zero
  else
    let rec loop x_prev x_curr =
      if Z.geq x_curr x_prev then x_prev
      else
        let next_x = Z.div (Z.add x_curr (Z.div target x_curr)) (Z.of_int 2) in
        loop x_curr next_x
    in
    let initial_guess = Z.div target (Z.of_int 2) in
    if Z.equal initial_guess Z.zero then Z.one
    else loop target initial_guess

let () =
    let precision_digits = 100 in
    (* base is Z.t, exponent is standard int *)
    let scale = Z.pow (Z.of_int 10) Stdlib.(precision_digits * 2) in
    let target = Z.mul (Z.of_int 2) scale in
    (*

    let target = Z.mul (Z.of_int 2) scale dol in

    *)
    let root = integer_sqrt target in
    let root_str = Z.to_string root in

    (* let int_part = String.sub root_str 0 1 in
    let frac_part = String.sub root_str 1 precision_digits in *)

    Printf.printf "Square root of 2 to %d decimal places:\n" precision_digits;
    (* Printf.printf "%s.%s\n" (Z.to_string int_part frac_part *)
    Printf.printf "done %s\n" root_str

(* let () =
    let x = Z.of_string "10000" in 
    Printf.printf "%s!=%s\n" (Z.to_string x) (Z.to_string (factorial_jay x))
    (* let big_num1 = Z.of_string "123456789012345678901234567890" in
    let big_num2 = Z.of_int 5000 in

    (* 2. Basic Arithmetic *)
    (* We use 'let open Z in' so +, -, *, and / work with big integers *)
    let sum = big_num1 + big_num2 in
    let product, quotient = (big_num1 * big_num2, big_num1 / big_num2) in

    (* 3. Printing Results *)
    (* We convert the Z.t values back to strings to print them *)
    Printf.printf "Big Number 1: %s\n" (Z.to_string big_num1);
    Printf.printf "Big Number 2: %s\n" (Z.to_string big_num2);
    Printf.printf "Sum:          %s\n" (Z.to_string sum);
    Printf.printf "Product:      %s\n" (Z.to_string product);
    Printf.printf "Quotient:     %s\n\n" (Z.to_string quotient);

    (* 4. Comparisons and Power Functions *)
    (* Z.pow takes a Z.t base and a native native 'int' exponent *)
    let two_power_100 = Z.pow (Z.of_int 2) 100 in

    Printf.printf "2^100 = %s\n" (Z.to_string two_power_100);

    (* Comparing two big integers *)
    if Z.gt two_power_100 big_num1 then
    Printf.printf "Result: 2^100 is larger than Big Number 1!\n"
    else
    Printf.printf "Result: Big Number 1 is larger than 2^100!\n" *) *)