type expr =
    | Number of int
    | Variable of string
    | Add of expr * expr
    | Multiply of expr * expr

let rec eval env e =
    match e with
    (* | Number n -> n *)
    | Variable name -> List.assoc name env
    | Add (a, b) -> eval env a + eval env b
    | Multiply (a, b) -> eval env a * eval env b
    | Number n -> n

let rec simplify e =
    match e with
    | Add (Number 0, x)
    | Add (x, Number 0) -> simplify x

    | Multiply (Number 1, x)
    | Multiply (x, Number 1) -> simplify x

    | Multiply (Number 0, _)
    | Multiply (_, Number 0) -> Number 0

    | Add (a, b) -> Add (simplify a, simplify b)

    | Multiply (a, b) -> Multiply (simplify a, simplify b)

    | _ -> e

let rec to_string e =
    match e with
    | Number n -> string_of_int n
    | Variable s -> s
    | Add (a, b) -> "(" ^ to_string a ^ " + " ^ to_string b ^ ")"
    | Multiply (a, b) -> "(" ^ to_string a ^ " * " ^ to_string b ^ ")"

let expr =
    Multiply (
        Add (Variable "x", Variable "y"),
        Variable "x"
    )

let simplified = simplify expr

let () =
    print_endline (to_string expr);
    print_endline (to_string simplified);

    let result = eval [("x", 20000); ("y", 1000)] expr in
    Printf.printf "Result = %d\n" result