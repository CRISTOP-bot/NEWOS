// Tema claro/oscuro (persistido), header con scroll, animaciones .aos,
// copy buttons + filtros de tablas + hamburguesa + volver arriba (no deps)
(function () {
  document.documentElement.classList.add("js");
  const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)");
  const SUN = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>';
  const MOON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8Z"/></svg>';
  const applyTheme = (t) => {
    document.documentElement.setAttribute("data-theme", t);
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", t === "dark" ? "#101722" : "#f3f2ee");
    const btn = document.getElementById("themeBtn");
    if (btn) { btn.innerHTML = t === "dark" ? SUN : MOON; btn.setAttribute("aria-label", t === "dark" ? "Cambiar a tema claro" : "Cambiar a tema oscuro"); btn.setAttribute("title", t === "dark" ? "Cambiar a tema claro" : "Cambiar a tema oscuro"); }
  };
  const getTheme = () => document.documentElement.getAttribute("data-theme") || "light";
  const save = (t) => { try { localStorage.setItem("newos-theme", t); } catch {} };

  document.addEventListener("DOMContentLoaded", () => {
    applyTheme(getTheme());

    const themeBtn = document.getElementById("themeBtn");
    if (themeBtn) themeBtn.addEventListener("click", () => {
      const next = getTheme() === "dark" ? "light" : "dark";
      document.documentElement.classList.add("theme-transition");
      applyTheme(next); save(next);
      setTimeout(() => document.documentElement.classList.remove("theme-transition"), 350);
    });

    // Header gana fondo al hacer scroll
    const nav = document.querySelector("nav");
    const onScroll = () => { if (nav) nav.classList.toggle("scroll", scrollY > 10); };
    addEventListener("scroll", onScroll, { passive: true });
    onScroll();

    // Animaciones de entrada
    const items = document.querySelectorAll(".aos,.aos-fade");
    if (reduceMotion.matches || !("IntersectionObserver" in window)) {
      items.forEach((el) => el.classList.add("animated"));
    } else {
      const io = new IntersectionObserver((entries) => {
        entries.forEach((e) => {
          if (e.isIntersecting) { e.target.classList.add("animated"); io.unobserve(e.target); }
        });
      }, { rootMargin: "0px 0px -40px 0px" });
      items.forEach((el) => io.observe(el));
    }

    // Botones copiar: excluir el propio control y admitir navegadores sin Clipboard API.
    document.querySelectorAll("pre.block").forEach((pre) => {
      pre.tabIndex = 0;
      pre.setAttribute("role", "region");
      if (!pre.hasAttribute("aria-label")) pre.setAttribute("aria-label", "Bloque de código; usa desplazamiento horizontal si hace falta");
      const b = document.createElement("button");
      b.className = "copy"; b.type = "button"; b.textContent = "Copiar";
      b.setAttribute("aria-label", "Copiar código al portapapeles");
      b.onclick = async () => {
        const code = pre.cloneNode(true);
        code.querySelectorAll("button.copy").forEach((button) => button.remove());
        const value = code.textContent.replace(/\\s+$/, "");
        try {
          if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(value);
          else {
            const helper = document.createElement("textarea");
            helper.value = value; helper.setAttribute("readonly", "");
            helper.style.position = "fixed"; helper.style.opacity = "0";
            document.body.appendChild(helper); helper.select();
            const copied = document.execCommand("copy"); helper.remove();
            if (!copied) throw new Error("clipboard unavailable");
          }
          b.textContent = "Copiado"; b.setAttribute("aria-label", "Código copiado");
        } catch {
          b.textContent = "No se pudo copiar"; b.setAttribute("aria-label", "No se pudo copiar el código");
        }
        setTimeout(() => { b.textContent = "Copiar"; b.setAttribute("aria-label", "Copiar código al portapapeles"); }, 1600);
      };
      pre.appendChild(b);
    });

    // Las regiones con scroll horizontal se pueden enfocar y anunciar.
    document.querySelectorAll(".tbl-wrap").forEach((region, index) => {
      region.tabIndex = 0;
      region.setAttribute("role", "region");
      if (!region.hasAttribute("aria-label")) {
        const heading = region.previousElementSibling;
        region.setAttribute("aria-label", heading?.textContent?.trim() || `Tabla de datos ${index + 1}`);
      }
    });

    // Filtros de tablas
    const wire = (inputId, tableId) => {
      const inp = document.getElementById(inputId), tbl = document.getElementById(tableId);
      if (!inp || !tbl) return;
      inp.addEventListener("input", () => {
        const q = inp.value.toLowerCase();
        tbl.querySelectorAll("tbody tr").forEach((tr) => {
          tr.style.display = tr.innerText.toLowerCase().includes(q) ? "" : "none";
        });
      });
    };
    wire("q-sys", "t-sys"); wire("q-cmd", "t-cmd"); wire("q-docs", "t-docs");

    // Navegación: sigue visible si JS no carga; al iniciar, mejora progresivamente.
    document.querySelectorAll(".hamb").forEach((h) => {
      const links = h.closest("nav")?.querySelector(".links");
      if (!links) return;
      links.classList.add("js-menu");
      h.setAttribute("aria-controls", "site-navigation");
      if (!links.id) links.id = "site-navigation";
      const closeMenu = (returnFocus = false) => {
        links.classList.remove("open");
        h.setAttribute("aria-expanded", "false");
        h.setAttribute("aria-label", "Abrir menú");
        if (returnFocus) h.focus();
      };
      h.setAttribute("aria-expanded", links.classList.contains("open") ? "true" : "false");
      h.addEventListener("click", () => {
        const open = !links.classList.contains("open");
        links.classList.toggle("open", open);
        h.setAttribute("aria-expanded", String(open));
        h.setAttribute("aria-label", open ? "Cerrar menú" : "Abrir menú");
        if (open) links.querySelector("a")?.focus({ preventScroll: true });
      });
      links.querySelectorAll("a").forEach((a) => a.addEventListener("click", () => closeMenu(false)));
      document.addEventListener("keydown", (e) => {
        if (e.key === "Escape" && links.classList.contains("open")) closeMenu(true);
      });
      document.addEventListener("click", (e) => {
        if (!links.contains(e.target) && !h.contains(e.target)) closeMenu(false);
      });
      addEventListener("resize", () => {
        if (matchMedia("(min-width: 761px)").matches) closeMenu();
      });
    });

    // Volver arriba
    const top = document.getElementById("backtop");
    if (top) {
      addEventListener("scroll", () => { top.style.display = scrollY > 600 ? "block" : "none"; }, { passive: true });
      top.onclick = () => scrollTo({ top: 0, behavior: reduceMotion.matches ? "auto" : "smooth" });
    }
  });
})();
