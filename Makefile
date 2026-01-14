.PHONY: help
.DEFAULT_GOAL := help

help:
	@echo ""
	@echo "Available targets:"
	@echo "  make docs    - Generate HTML and PDF documentation"
	@echo "  make clean-docs - Clean generated documentation files"
	@echo ""

DOXYGEN = doxygen
DOXYFILE = docs/Doxyfile
DOXYGEN_THEME_PATH = docs/themes/doxygen-awesome-css

docs: $(DOXYGEN_THEME_PATH)
	@echo "Generating documentation with ${DOXYFILE}..."
	$(DOXYGEN) $(DOXYFILE)
	@if [ -d "docs/latex" ]; then \
		echo "Building PDF documentation..."; \
		$(MAKE) -C docs/latex; \
		rm -rf docs/latex/rtos_data_acquisition.pdf || true; \
		mv docs/latex/refman.pdf docs/latex/rtos_data_acquisition.pdf; \
	fi
	@echo ""
	@echo "Documentation generated:"
	@echo "  - HTML: docs/html/index.html"
	@if [ -d "docs/latex" ]; then \
		echo "  - PDF: docs/latex/rtos_data_acquisition.pdf"; \
	fi

clean-docs:
	rm -rf docs/html/*
	rm -rf docs/latex/*
	@echo "Cleaned documentation directories."

$(DOXYGEN_THEME_PATH):
	git submodule update --init --recursive
