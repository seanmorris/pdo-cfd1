# Expand JS includes before the C preprocessor stringifies the EM_JS bodies.
# PHP substitutes srcdir/builddir when it includes this fragment at configure time.
PDO_CFD1_JS_TEMPLATE = $(srcdir)/pdo_cfd1_js.h.in
PDO_CFD1_JS_INPUTS = $(addprefix $(srcdir)/,$(shell sed -n 's/^\#include "\(pdo_cfd1_[a-z_]*\.js\)"$$/\1/p' "$(PDO_CFD1_JS_TEMPLATE)"))

$(builddir)/pdo_cfd1.lo: $(builddir)/generated/pdo_cfd1_js.h

$(builddir)/generated/pdo_cfd1_js.h: $(PDO_CFD1_JS_TEMPLATE) $(PDO_CFD1_JS_INPUTS) $(srcdir)/Makefile.frag
	@mkdir -p "$(@D)"
	@set -eu; \
		trap 'rm -f "$@.tmp"' 0 1 2 3 15; \
		awk -v source="$(srcdir)" ' \
			/^\#include "pdo_cfd1_[a-z_]*\.js"$$/ { \
				file = source "/" substr($$2, 2, length($$2) - 2); \
				while ((status = (getline line < file)) > 0) print line; \
				if (status < 0) { print "Cannot read " file > "/dev/stderr"; exit 1; } \
				close(file); \
				next; \
			} \
			{ print }' "$(PDO_CFD1_JS_TEMPLATE)" > "$@.tmp"; \
		mv "$@.tmp" "$@"

.PHONY: clean-pdo-cfd1-js
clean: clean-pdo-cfd1-js
clean-pdo-cfd1-js:
	rm -f "$(builddir)/generated/pdo_cfd1_js.h" "$(builddir)/generated/pdo_cfd1_js.h.tmp"
