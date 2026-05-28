
(* let rec sort lst = match lst with [] -> [] | head :: tail -> insert head (sort tail)
and insert elt lst = match lst with [] -> [elt] | head :: tail -> if elt <= head then elt :: lst else head :: insert elt tail;;
 *)

let rec insert elt lst = match lst with [] -> [elt] | head :: tail -> if elt <= head then elt :: lst else head :: insert elt tail
let rec sort lst = match lst with [] -> [] | head :: tail -> insert head (sort tail)

let l = ["is"; "a"; "tale"; "told"; "etc."]

let () = 
    Printf.printf "Foo bar\n"

(* let rec test_named_params c ~a ~b = 
    a + 2 * b - c


let () =
    Printf.printf "Foo bar %d\n" (test_named_params 3 ~b:2 ~a:1)


     *)