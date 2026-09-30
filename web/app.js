// Tema claro/oscuro (persistido), header con scroll, animaciones .aos,
// copy buttons + filtros de tablas + hamburguesa + volver arriba (no deps)
(function () {
  const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)");
  const SUN = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>';
  const MOON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8Z"/></svg>';
  const applyTheme = (t) => {
    document.documentElement.setAttribute("data-theme", t);
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", t === "dark" ? "#1a0f04" : "#ffffff");
    const btn = document.getElementById("themeBtn");
    if (btn) { btn.innerHTML = t === "dark" ? SUN : MOON; btn.setAttribute("aria-label", t === "dark" ? "Cambiar a tema claro" : "Cambiar a tema oscuro"); }
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

    // Botones copiar en bloques de código
    document.querySelectorAll("pre.block").forEach((pre) => {
      const b = document.createElement("button");
      b.className = "copy"; b.textContent = "copiar";
      b.onclick = async () => {
        try { await navigator.clipboard.writeText(pre.innerText.replace("copiar\n", "")); b.textContent = "✓"; }
        catch { b.textContent = "error"; }
        setTimeout(() => (b.textContent = "copiar"), 1200);
      };
      pre.appendChild(b);
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

    // Hamburguesa móvil
    document.querySelectorAll(".hamb").forEach((h) => {
      h.addEventListener("click", () => {
        const links = h.closest("nav")?.querySelector(".links") || h.parentElement.querySelector(".links");
        if (links) links.classList.toggle("open");
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
