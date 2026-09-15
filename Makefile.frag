# Expand includes without expanding macros in JS, before EM_JS stringifies it.
# Leave EM_JS undefined here; pdo_cfd1.c includes emscripten.h during compilation.
# PHP substitutes srcdir/builddir when it includes this fragment at configure time.
PDO_CFD1_JS_TEMPLATE = $(srcdir)/pdo_cfd1_js.h.in
PDO_CFD1_JS_HEADER = $(builddir)/generated/pdo_cfd1_js.h
PDO_CFD1_JS_DEPS = $(builddir)/generated/pdo_cfd1_js.d

$(builddir)/pdo_cfd1.lo: $(PDO_CFD1_JS_HEADER)

# Group both outputs so parallel builds and a missing depfile run one compiler.
$(PDO_CFD1_JS_HEADER) $(PDO_CFD1_JS_DEPS) &: $(PDO_CFD1_JS_TEMPLATE) $(srcdir)/Makefile.frag
	@mkdir -p "$(dir $(PDO_CFD1_JS_HEADER))"
	@set -eu; \
		trap 'rm -f "$(PDO_CFD1_JS_HEADER).tmp" "$(PDO_CFD1_JS_DEPS).tmp"' 0 1 2 3 15; \
		$(CC) -E -P -CC -fdirectives-only -x c \
			-MMD -MP -MF "$(PDO_CFD1_JS_DEPS).tmp" \
			-MQ "$(PDO_CFD1_JS_HEADER)" -MQ "$(PDO_CFD1_JS_DEPS)" \
			"$(PDO_CFD1_JS_TEMPLATE)" -o "$(PDO_CFD1_JS_HEADER).tmp"; \
		mv "$(PDO_CFD1_JS_HEADER).tmp" "$(PDO_CFD1_JS_HEADER)"; \
		mv "$(PDO_CFD1_JS_DEPS).tmp" "$(PDO_CFD1_JS_DEPS)"

# Do not regenerate included makefiles while removing build outputs.
ifeq ($(filter clean distclean clean-pdo-cfd1-js,$(MAKECMDGOALS)),)
include $(PDO_CFD1_JS_DEPS)
endif

.PHONY: clean-pdo-cfd1-js
clean: clean-pdo-cfd1-js
clean-pdo-cfd1-js:
	rm -f "$(PDO_CFD1_JS_HEADER)" "$(PDO_CFD1_JS_DEPS)" "$(PDO_CFD1_JS_HEADER).tmp" "$(PDO_CFD1_JS_DEPS).tmp"
