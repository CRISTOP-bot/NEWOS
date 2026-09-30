// Demo interactiva del shell nsh (simulación en el navegador con salidas reales.
// El shell de verdad vive en QEMU: `make qemu-limine`).
(function () {
  const out = document.getElementById("demo-out");
  const input = document.getElementById("demo-in-field");
  if (!out || !input) return;
  const t0 = Date.now();
  const TOOLS = "ls cat echo mkdir rmdir rm touch pwd clear uname hostname whoami id date uptime ps kill sleep free df wc head grep sort img vid ltest about newfetch desktop newpkg hello sh".split(" ");
  const FILES = {
    "/etc/hostname": "newos",
    "/etc/version": "NEWOS 0.2.0-pre-alpha",
    "/etc/motd": "NEWOS 0.2.0-pre-alpha (x86_64) - nsh shell\nType 'help' for commands. Spanish keyboard ready.",
    "/etc/passwd": "root:x:0:0:root:/root:/init\nuser:x:1000:1000:user:/home/user:/init",
    "/root/README": "Welcome to NEWOS. Type 'help'.",
    "/home/user/README": "Your home. Try: ls /bin",
    "/tmp/hello-new-1.0.0-x86_64.new": "<NEW1 package: hello-new 1.0.0>",
  };
  const DIRS = ["/", "/bin", "/etc", "/tmp", "/dev", "/proc", "/sys", "/home", "/home/user", "/root", "/var", "/var/lib/newpkg", "/sbin", "/lib", "/usr", "/opt", "/mnt", "/run"];
  let cwd = "/";
  let hist = [];
  const ps1 = () => "root@newos:" + cwd + "# ";
  function print(t) { out.textContent += t + "\n"; out.scrollTop = out.scrollHeight; }
  function uptime() {
    const s = Math.floor((Date.now() - t0) / 1000);
    return "Uptime: 0h 0m " + s + "s";
  }
  function newfetch() {
    return [" █▄ █ █▀▀ █░ █░ █▀█ █▀▀   root@newos",
      " █ ▀█ █▀▀ █▄▀█ █▄█ ▀▀█   OS: NEWOS 0.2.0-pre-alpha",
      " ▀  ▀ ▀▀▀ ▀ ▀▀ ▀ ▀▀ ▀▀▀   Kernel: 0.2.0-pre-alpha (x86_64)",
      "                          " + uptime(),
      "                          Memory: 11 / 126 MiB",
      "                          Shell: nsh (/init)",
      "                          Display: demo navegador (QEMU: 1280x800 x32)",
      "                          Packages: 33 (/bin)"].join("\n");
  }
  function resolve(p) {
    if (!p || p === ".") return cwd;
    if (p === "..") return cwd === "/" ? "/" : cwd.slice(0, cwd.lastIndexOf("/")) || "/";
    if (!p.startsWith("/")) p = (cwd === "/" ? "/" : cwd + "/") + p;
    const parts = [];
    p.split("/").forEach((x) => { if (x && x !== ".") { if (x === "..") parts.pop(); else parts.push(x); } });
    return "/" + parts.join("/");
  }
  const CMDS = {
    help: () => "Comandos: cd exit help history clear\nBinarios /bin: " + TOOLS.join(" ") + "\nPrueba: newfetch · about · ls /bin · cat /etc/motd · ps · newpkg list",
    ls: (a) => { const d = resolve(a[0] || cwd); if (d === "/bin") return TOOLS.join("  "); if (DIRS.includes(d)) return { "/": "bin sbin lib etc usr home root run opt var mnt tmp dev proc sys", "/etc": "hostname passwd motd version splash.bmp test.ppm", "/tmp": "hello-new-1.0.0-x86_64.new", "/var/lib/newpkg": "(vacío: instala algo con newpkg install)", "/home/user": "README", "/root": "README" }[d] || "(vacío)"; return "ls: no such directory: " + (a[0] || ""); },
    echo: (a, raw) => { let s = raw.replace(/^echo\s*/, ""); const nn = s.startsWith("-n "); if (nn) s = s.slice(3); return s + (nn ? "" : ""); },
    cat: (a) => { if (!a[0]) return "uso: cat <fichero>"; const f = resolve(a[0]); return FILES[f] !== undefined ? FILES[f] : "cat: no such file: " + a[0]; },
    pwd: () => cwd,
    cd: (a) => { if (!a[0]) { cwd = "/home/user"; return ""; } const d = resolve(a[0]); if (DIRS.includes(d)) { cwd = d; return ""; } return "cd: no such directory: " + a[0]; },
    clear: () => { out.textContent = ""; return ""; },
    history: () => hist.map((h, i) => (i + 1) + "  " + h).join("\n") || "(vacío)",
    exit: () => "demo: aquí no se sale, esto es un navegador. Prueba 'clear'.",
    whoami: () => "root",
    id: () => "uid=0(root) gid=0(root)",
    hostname: (a) => a[0] ? (FILES["/etc/hostname"] = a[0], "") : FILES["/etc/hostname"],
    uname: (a) => a.includes("-r") ? "0.2.0-pre-alpha" : a.includes("-m") ? "x86_64" : a.includes("-s") ? "NEWOS" : "NEWOS 0.2.0-pre-alpha x86_64",
    date: () => new Date().toString(),
    uptime: () => uptime(),
    ps: () => "PID  PPID STATE NAME\n1    0    run   /init\n4    1    run   nsh (demo)",
    kill: () => "kill: en la demo nadie muere. En NEWOS: kill <pid> (init protegido).",
    sleep: () => "sleep: en la demo el tiempo es relativo. En NEWOS: sleep <ms>.",
    free: () => "total: 129024 KiB   free: 117760 KiB",
    df: () => "tmpfs  132120576 bytes total, 120586240 free (RAM, volátil)",
    pwd2: () => cwd,
    about: () => "NEWOS - a from-scratch x86_64 operating system\nversion: NEWOS 0.2.0-pre-alpha\nhost: newos\nmemory: 126 MiB total / 115 MiB free\n" + uptime() + "\nlicense: MIT",
    newfetch: () => newfetch(),
    hello: () => "Hello from ring 3! (demo)",
    ltest: () => "libc self-test: string PASS, stdio PASS, heap PASS → exit 0",
    newpkg: (a) => {
      if (a[0] === "list") return "hello-new 1.0.0";
      if (a[0] === "files") return "/bin/hello-new";
      if (a[0] === "info") return "name: hello-new\nversion: 1.0.0\narch: x86_64\nlicense: MIT";
      if (a[0] === "verify" || a[0] === "install" || a[0] === "remove") return "newpkg " + a[0] + ": esto requiere el NEWOS real (make qemu). Ver página Paquetes.";
      return "uso: newpkg {info|verify|install|remove|list|files}";
    },
    img: () => "img: necesita el framebuffer real (make qemu-limine). En NEWOS abre BMP/PPM.",
    vid: () => "vid: animación procedural de 60 frames. Solo en QEMU con Limine.",
    desktop: () => "desktop: el compositor real está en la captura de abajo. Requiere QEMU.",
  };
  ["wc", "head", "grep", "sort", "mkdir", "rmdir", "rm", "touch"].forEach((c) => { CMDS[c] = () => c + ": demo parcial — el binario real vive en /bin de NEWOS."; });
  function run(line) {
    print(ps1() + line);
    const l = line.trim();
    if (!l) return;
    hist.push(l);
    const parts = l.split(/\s+/);
    const cmd = parts[0], args = parts.slice(1);
    const fn = CMDS[cmd];
    if (!fn) { print("nsh: command not found: " + cmd); return; }
    const r = fn(args, l);
    if (r) print(r);
  }
  print("NEWOS 0.2.0-pre-alpha (demo en navegador) - nsh shell");
  print("Escribe 'help'. El sistema real arranca con: make qemu-limine");
  print(ps1() + "newfetch");
  print(newfetch());
  let hi = -1;
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { run(input.value); input.value = ""; hi = -1; }
    else if (e.key === "ArrowUp") { if (hist.length) { hi = hi < 0 ? hist.length - 1 : Math.max(0, hi - 1); input.value = hist[hi]; } e.preventDefault(); }
    else if (e.key === "ArrowDown") { if (hi >= 0) { hi++; input.value = hist[hi] || ""; if (hi >= hist.length) hi = -1; } e.preventDefault(); }
    else if (e.key === "l" && e.ctrlKey) { out.textContent = ""; input.value = ""; e.preventDefault(); }
  });
  document.getElementById("demo-term").addEventListener("click", () => input.focus());
})();
