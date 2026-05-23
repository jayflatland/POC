TARGETS = \
    factorial \
    hello \
    leibniz \
    symbol_math_expressions \
    sqrt_2

all: $(TARGETS)

%: %.ml
	ocamlfind ocamlc -linkpkg -package zarith -o $@ $<

clean:
	rm -f $(TARGETS) *.cmi *.cmo