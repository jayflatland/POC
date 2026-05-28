TARGETS = \
    factorial \
    hello \
    leibniz \
    symbol_math_expressions \
    sqrt_2 \
    random_ocaml_crap \
    sockets

all: $(TARGETS)

%: %.ml
	ocamlfind ocamlc unix.cma -linkpkg -package zarith -o $@ $<

clean:
	rm -f $(TARGETS) *.cmi *.cmo