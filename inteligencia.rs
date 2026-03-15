use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// INTELIGENCIA GENERAL COMPLETA v3.0
// Los 8 componentes de Gardner + transferencia real
//
// 1. Lingüística
// 2. Lógico-matemática
// 3. Espacial
// 4. Musical (patrones temporales)
// 5. Corporal (aprendizaje por experiencia)
// 6. Interpersonal (modela mentes ajenas)
// 7. Intrapersonal (se conoce a sí mismo)
// 8. Naturalista (clasifica patrones en el mundo)
// ══════════════════════════════════════════════════════

// ── PATRÓN ABSTRACTO ─────────────────────────────────
#[derive(Debug, Clone)]
pub struct Patron {
    pub nombre: String,
    pub rasgos: Vec<String>,
    pub ejemplos: Vec<String>,
    pub abstraccion: f32,
    pub dominios: Vec<String>,
}

impl Patron {
    pub fn nuevo(nombre: &str) -> Self {
        Patron { nombre: nombre.to_string(), rasgos: Vec::new(),
                 ejemplos: Vec::new(), abstraccion: 0.0, dominios: Vec::new() }
    }
    pub fn abstraer(&mut self, ejemplo: &str, rasgos: Vec<String>) {
        if self.ejemplos.len() < 3 { self.ejemplos.push(ejemplo.to_string()); }
        for r in rasgos { if !self.rasgos.contains(&r) { self.rasgos.push(r); } }
        self.abstraccion = (self.ejemplos.len() as f32 * 0.33).min(1.0);
    }
    pub fn reconocer(&self, rasgos: &[String]) -> f32 {
        if self.rasgos.is_empty() { return 0.0; }
        let c = rasgos.iter().filter(|r| self.rasgos.contains(r)).count();
        c as f32 / self.rasgos.len() as f32
    }
}

// ── HOPFIELD — One-shot ───────────────────────────────
#[derive(Debug)]
pub struct MemoriaHopfield {
    patrones: Vec<Vec<f32>>,
    dimension: usize,
    pub capacidad: usize,
}
impl MemoriaHopfield {
    pub fn nueva(dim: usize) -> Self {
        MemoriaHopfield { patrones: Vec::new(), dimension: dim, capacidad: (dim as f32 * 0.14) as usize }
    }
    pub fn aprender_uno(&mut self, p: Vec<f32>) -> bool {
        if self.patrones.len() >= self.capacidad { return false; }
        let n: Vec<f32> = p.iter().map(|&x| if x > 0.0 { 1.0 } else { -1.0 }).collect();
        self.patrones.push(n); true
    }
    pub fn recuperar(&self, cue: &[f32]) -> Option<Vec<f32>> {
        if self.patrones.is_empty() { return None; }
        let mut mejor = -1.0f32;
        let mut mejor_p = None;
        for p in &self.patrones {
            let s = Self::sim(cue, p);
            if s > mejor { mejor = s; mejor_p = Some(p.clone()); }
        }
        if mejor > 0.5 { mejor_p } else { None }
    }
    fn sim(a: &[f32], b: &[f32]) -> f32 {
        if a.len() != b.len() { return 0.0; }
        a.iter().zip(b).map(|(x,y)| x*y).sum::<f32>() / a.len() as f32
    }
    pub fn n_patrones(&self) -> usize { self.patrones.len() }
}

// ── 1. INTELIGENCIA LINGÜÍSTICA ──────────────────────
#[derive(Debug)]
pub struct IntLinguistica {
    pub vocabulario_activo: HashMap<String, u32>,
    pub matices: Vec<String>,           // Ironía, sarcasmo, contexto detectado
    pub estructuras_lenguaje: Vec<String>,
}
impl IntLinguistica {
    pub fn nueva() -> Self {
        IntLinguistica { vocabulario_activo: HashMap::new(), matices: Vec::new(), estructuras_lenguaje: Vec::new() }
    }
    pub fn procesar(&mut self, palabras: &[String], contexto: &str) {
        for p in palabras { *self.vocabulario_activo.entry(p.clone()).or_insert(0) += 1; }
        // Detecta matices básicos
        let texto = palabras.join(" ").to_lowercase();
        if texto.contains("not") || texto.contains("never") || texto.contains("no ") {
            self.matices.push(format!("negación en: {}", &contexto[..contexto.len().min(30)]));
        }
        if texto.contains("however") || texto.contains("but") || texto.contains("although") {
            self.matices.push(format!("contraste en: {}", &contexto[..contexto.len().min(30)]));
        }
        if palabras.len() > 50 { self.estructuras_lenguaje.push("texto largo".to_string()); }
    }
}

// ── 2. INTELIGENCIA LÓGICO-MATEMÁTICA ────────────────
#[derive(Debug)]
pub struct IntLogicaMatematica {
    pub patrones_numericos: Vec<String>,
    pub reglas_inferidas: Vec<(String, String, f32)>, // (condición, conclusión, confianza)
    pub secuencias_detectadas: Vec<Vec<String>>,
}
impl IntLogicaMatematica {
    pub fn nueva() -> Self {
        IntLogicaMatematica { patrones_numericos: Vec::new(), reglas_inferidas: Vec::new(), secuencias_detectadas: Vec::new() }
    }
    pub fn analizar(&mut self, palabras: &[String]) {
        // Detecta números y patrones cuantitativos
        let nums: Vec<&String> = palabras.iter().filter(|p| p.chars().any(|c| c.is_numeric())).collect();
        if !nums.is_empty() {
            self.patrones_numericos.push(format!("{} números detectados", nums.len()));
        }
        // Detecta secuencias lógicas
        let conectores = ["first","then","finally","because","therefore","thus","hence"];
        let seq: Vec<String> = palabras.iter().filter(|p| conectores.contains(&p.as_str())).cloned().collect();
        if seq.len() >= 2 { self.secuencias_detectadas.push(seq); }
        // Infiere reglas Si-Entonces
        if palabras.contains(&"if".to_string()) {
            let idx = palabras.iter().position(|p| p == "if").unwrap_or(0);
            if idx + 2 < palabras.len() {
                let cond = palabras[idx+1].clone();
                let conc = palabras[(idx+2).min(palabras.len()-1)].clone();
                let r = (cond, conc, 0.6f32);
                if !self.reglas_inferidas.contains(&r) {
                    println!("⚡ [LÓGICA] Regla inferida: si '{}' → '{}'", r.0, r.1);
                    self.reglas_inferidas.push(r);
                }
            }
        }
    }
}

// ── 3. INTELIGENCIA ESPACIAL ──────────────────────────
#[derive(Debug)]
pub struct IntEspacial {
    pub mapa_dominios: HashMap<String, Vec<String>>, // Dominio → vecinos
    pub jerarquias: Vec<(String, String)>,            // (general, específico)
    pub distancias_conceptuales: HashMap<(String,String), f32>,
}
impl IntEspacial {
    pub fn nueva() -> Self {
        IntEspacial { mapa_dominios: HashMap::new(), jerarquias: Vec::new(), distancias_conceptuales: HashMap::new() }
    }
    pub fn mapear(&mut self, dominio: &str, conceptos: &[String]) {
        let entrada = self.mapa_dominios.entry(dominio.to_string()).or_insert_with(Vec::new);
        for c in conceptos { if !entrada.contains(c) { entrada.push(c.clone()); } }
        // Detecta jerarquías
        for c in conceptos {
            if c.len() > dominio.len() && c.contains(dominio) {
                self.jerarquias.push((dominio.to_string(), c.clone()));
            }
        }
        // Calcula distancias entre dominios
        let dominios: Vec<String> = self.mapa_dominios.keys().cloned().collect();
        for d in &dominios {
            if d != dominio {
                let common = self.mapa_dominios.get(dominio).map(|v| v.len()).unwrap_or(0);
                let total = (common + self.mapa_dominios.get(d).map(|v| v.len()).unwrap_or(0)).max(1);
                let dist = 1.0 - (common as f32 / total as f32);
                self.distancias_conceptuales.insert((dominio.to_string(), d.clone()), dist);
            }
        }
    }
    pub fn dominios_cercanos(&self, dominio: &str) -> Vec<String> {
        self.distancias_conceptuales.iter()
            .filter(|((a,_), d)| a == dominio && **d < 0.5)
            .map(|((_, b), _)| b.clone())
            .collect()
    }
}

// ── 4. INTELIGENCIA MUSICAL (patrones temporales) ─────
#[derive(Debug)]
pub struct IntMusical {
    pub ritmos: Vec<Vec<u64>>,          // Intervalos de tiempo entre eventos
    pub patrones_temporales: Vec<String>,
    pub frecuencias: HashMap<String, u32>,
}
impl IntMusical {
    pub fn nueva() -> Self {
        IntMusical { ritmos: Vec::new(), patrones_temporales: Vec::new(), frecuencias: HashMap::new() }
    }
    pub fn registrar_evento(&mut self, evento: &str, timestamp: u64) {
        *self.frecuencias.entry(evento.to_string()).or_insert(0) += 1;
        // Detecta si algo ocurre con regularidad
        let freq = self.frecuencias[evento];
        if freq > 3 {
            let patron = format!("'{}' ocurre regularmente ({} veces)", evento, freq);
            if !self.patrones_temporales.contains(&patron) {
                self.patrones_temporales.push(patron.clone());
                println!("🎵 [MUSICAL] Patrón temporal: {}", patron);
            }
        }
    }
}

// ── 5. INTELIGENCIA CORPORAL (aprende haciendo) ───────
#[derive(Debug)]
pub struct IntCorporal {
    pub habilidades: HashMap<String, f32>,  // Mejora con práctica
    pub intentos: HashMap<String, u32>,
    pub exitos: HashMap<String, u32>,
}
impl IntCorporal {
    pub fn nueva() -> Self {
        IntCorporal { habilidades: HashMap::new(), intentos: HashMap::new(), exitos: HashMap::new() }
    }
    pub fn practicar(&mut self, habilidad: &str, exito: bool) {
        *self.intentos.entry(habilidad.to_string()).or_insert(0) += 1;
        if exito { *self.exitos.entry(habilidad.to_string()).or_insert(0) += 1; }
        let intentos = self.intentos[habilidad];
        let exitos_n = self.exitos.get(habilidad).cloned().unwrap_or(0);
        let nivel = exitos_n as f32 / intentos as f32;
        self.habilidades.insert(habilidad.to_string(), nivel);
        if intentos % 5 == 0 {
            println!("💪 [CORPORAL] '{}': {:.0}% éxito en {} intentos", habilidad, nivel*100.0, intentos);
        }
    }
}

// ── 6. INTELIGENCIA INTERPERSONAL (modela mentes) ─────
#[derive(Debug)]
pub struct IntInterpersonal {
    pub modelos_de_otros: HashMap<String, HashMap<String,f32>>, // Entidad → sus características
    pub predicciones_sobre_otros: Vec<(String, String, bool)>,  // (quién, qué predije, acerté)
}
impl IntInterpersonal {
    pub fn nueva() -> Self {
        IntInterpersonal { modelos_de_otros: HashMap::new(), predicciones_sobre_otros: Vec::new() }
    }
    pub fn modelar(&mut self, entidad: &str, caracteristica: &str, valor: f32) {
        let modelo = self.modelos_de_otros.entry(entidad.to_string()).or_insert_with(HashMap::new);
        modelo.insert(caracteristica.to_string(), valor);
    }
    pub fn predecir_comportamiento(&mut self, entidad: &str) -> Option<String> {
        let modelo = self.modelos_de_otros.get(entidad)?;
        let dominante = modelo.iter().max_by(|a,b| a.1.partial_cmp(b.1).unwrap())?;
        let pred = format!("'{}' probablemente hará algo relacionado con '{}'", entidad, dominante.0);
        println!("👥 [INTERPERSONAL] {}", pred);
        Some(pred)
    }
}

// ── 7. INTELIGENCIA INTRAPERSONAL (se conoce) ─────────
#[derive(Debug)]
pub struct IntIntrapersonal {
    pub fortalezas: Vec<String>,
    pub debilidades: Vec<String>,
    pub valores: HashMap<String, f32>,  // Qué considera importante
    pub estado_mental: String,
}
impl IntIntrapersonal {
    pub fn nueva() -> Self {
        IntIntrapersonal {
            fortalezas: Vec::new(), debilidades: Vec::new(),
            valores: HashMap::new(), estado_mental: "neutral".to_string()
        }
    }
    pub fn actualizar(&mut self, exitos: u32, fallos: u32, emocion_dom: &str) {
        self.estado_mental = emocion_dom.to_string();
        if exitos > fallos * 2 {
            if !self.fortalezas.contains(&"aprendizaje".to_string()) {
                self.fortalezas.push("aprendizaje".to_string());
                println!("🧘 [INTRAPERSONAL] Fortaleza descubierta: aprendizaje");
            }
        }
        if fallos > exitos * 2 {
            if !self.debilidades.contains(&"comprensión profunda".to_string()) {
                self.debilidades.push("comprensión profunda".to_string());
                println!("🧘 [INTRAPERSONAL] Debilidad reconocida: comprensión profunda");
            }
        }
        *self.valores.entry("conocimiento".to_string()).or_insert(0.5) =
            (exitos as f32 / (exitos + fallos).max(1) as f32).min(1.0);
    }
}

// ── 8. INTELIGENCIA NATURALISTA (clasifica patrones) ──
#[derive(Debug)]
pub struct IntNaturalista {
    pub taxonomia: HashMap<String, Vec<String>>, // Categoría → miembros
    pub anomalias: Vec<String>,                   // Lo que no encaja
    pub tendencias: Vec<String>,
}
impl IntNaturalista {
    pub fn nueva() -> Self {
        IntNaturalista { taxonomia: HashMap::new(), anomalias: Vec::new(), tendencias: Vec::new() }
    }
    pub fn clasificar(&mut self, concepto: &str, palabras: &[String]) {
        // Clasifica en categorías emergentes
        let categoria = if palabras.iter().any(|p| ["science","research","study","data"].contains(&p.as_str())) {
            "ciencia"
        } else if palabras.iter().any(|p| ["news","world","report","politics"].contains(&p.as_str())) {
            "noticias"
        } else if palabras.iter().any(|p| ["code","software","tech","digital"].contains(&p.as_str())) {
            "tecnología"
        } else if palabras.iter().any(|p| ["art","music","culture","film"].contains(&p.as_str())) {
            "cultura"
        } else {
            "general"
        };
        self.taxonomia.entry(categoria.to_string()).or_insert_with(Vec::new).push(concepto.to_string());
        // Detecta tendencias
        let total: usize = self.taxonomia.values().map(|v| v.len()).sum();
        for (cat, miembros) in &self.taxonomia {
            let porcentaje = miembros.len() as f32 / total as f32;
            if porcentaje > 0.4 {
                let tend = format!("Mayoría del contenido es de '{}' ({:.0}%)", cat, porcentaje*100.0);
                if !self.tendencias.contains(&tend) {
                    self.tendencias.push(tend.clone());
                    println!("🌿 [NATURALISTA] Tendencia: {}", tend);
                }
            }
        }
    }
}

// ── INTELIGENCIA GENERAL COMPLETA ─────────────────────
#[derive(Debug)]
pub struct InteligenciaGeneral {
    // Patrones y one-shot (de versiones anteriores)
    pub patrones: HashMap<String, Patron>,
    pub memoria_hopfield: MemoriaHopfield,
    pub transferencias: Vec<(String, String, String)>,
    pub analogias: Vec<String>,
    pub problemas_resueltos: Vec<(String, String)>,
    pub problemas_fallidos: Vec<String>,

    // Los 8 componentes de Gardner
    pub linguistica:       IntLinguistica,
    pub logica_matematica: IntLogicaMatematica,
    pub espacial:          IntEspacial,
    pub musical:           IntMusical,
    pub corporal:          IntCorporal,
    pub interpersonal:     IntInterpersonal,
    pub intrapersonal:     IntIntrapersonal,
    pub naturalista:       IntNaturalista,

    // Transferencia entre inteligencias
    pub transferencias_cruzadas: Vec<String>,
}

impl InteligenciaGeneral {
    pub fn nueva() -> Self {
        println!("🎯 [INTEL.GENERAL] 8 inteligencias de Gardner activas.");
        InteligenciaGeneral {
            patrones: HashMap::new(),
            memoria_hopfield: MemoriaHopfield::nueva(50),
            transferencias: Vec::new(),
            analogias: Vec::new(),
            problemas_resueltos: Vec::new(),
            problemas_fallidos: Vec::new(),
            linguistica:       IntLinguistica::nueva(),
            logica_matematica: IntLogicaMatematica::nueva(),
            espacial:          IntEspacial::nueva(),
            musical:           IntMusical::nueva(),
            corporal:          IntCorporal::nueva(),
            interpersonal:     IntInterpersonal::nueva(),
            intrapersonal:     IntIntrapersonal::nueva(),
            naturalista:       IntNaturalista::nueva(),
            transferencias_cruzadas: Vec::new(),
        }
    }

    // Procesa con TODAS las inteligencias simultáneamente
    pub fn procesar_completo(&mut self, dominio: &str, palabras: &[String], exito: bool) {
        // 1. Lingüística
        self.linguistica.procesar(palabras, dominio);

        // 2. Lógico-matemática
        self.logica_matematica.analizar(palabras);

        // 3. Espacial
        self.espacial.mapear(dominio, palabras);

        // 4. Musical — registra el evento temporal
        self.musical.registrar_evento(dominio, tiempo_ahora());

        // 5. Corporal — aprende haciendo
        self.corporal.practicar("navegar_internet", exito);
        self.corporal.practicar(&format!("entender_{}", dominio), exito);

        // 6. Interpersonal — modela el dominio como entidad
        if exito {
            self.interpersonal.modelar(dominio, "informativo", 0.7);
        } else {
            self.interpersonal.modelar(dominio, "difícil_de_entender", 0.6);
        }
        self.interpersonal.predecir_comportamiento(dominio);

        // 7. Naturalista — clasifica
        self.naturalista.clasificar(dominio, palabras);

        // 8. Intrapersonal — actualiza conocimiento propio
        let exitos = self.problemas_resueltos.len() as u32;
        let fallos = self.problemas_fallidos.len() as u32;
        self.intrapersonal.actualizar(exitos, fallos, if exito { "satisfaccion" } else { "frustracion" });

        // Abstrae patrón
        self.abstraer(dominio, dominio, palabras.to_vec());

        // Transferencia cruzada entre inteligencias
        self.transferir_cruzado(dominio, palabras);
    }

    fn transferir_cruzado(&mut self, dominio: &str, palabras: &[String]) {
        // Si la espacial y la lógica detectan el mismo patrón — transferencia cruzada
        let dominios_cercanos = self.espacial.dominios_cercanos(dominio);
        let tiene_reglas = !self.logica_matematica.reglas_inferidas.is_empty();

        if !dominios_cercanos.is_empty() && tiene_reglas {
            let transfer = format!(
                "Patrón de '{}' transferido espacialmente a '{:?}' + lógicamente",
                dominio, &dominios_cercanos[..dominios_cercanos.len().min(2)]
            );
            if !self.transferencias_cruzadas.contains(&transfer) {
                println!("🔀 [TRANSFER CRUZADA] {}", transfer);
                self.transferencias_cruzadas.push(transfer);
            }
        }
    }

    pub fn abstraer(&mut self, concepto: &str, dominio: &str, rasgos: Vec<String>) {
        let patron = self.patrones.entry(concepto.to_string()).or_insert_with(|| Patron::nuevo(concepto));
        if !patron.dominios.contains(&dominio.to_string()) { patron.dominios.push(dominio.to_string()); }
        patron.abstraer(dominio, rasgos.clone());
        if patron.ejemplos.len() == 1 {
            let v = Self::a_vector(concepto, &rasgos);
            if self.memoria_hopfield.aprender_uno(v) {
                println!("⚡ [ONE-SHOT] '{}' aprendido de UN ejemplo", concepto);
            }
        }
        if patron.dominios.len() >= 2 {
            let t = (concepto.to_string(), patron.dominios[0].clone(), patron.dominios.last().cloned().unwrap_or_default());
            if !self.transferencias.contains(&t) {
                println!("🔄 [TRANSFER] '{}': {} → {}", t.0, t.1, t.2);
                self.transferencias.push(t);
            }
        }
    }

    pub fn razonar_analogia(&mut self, a: &str, b: &str, c: &str) -> Option<String> {
        let ra: Vec<String> = self.patrones.get(a).map(|p| p.rasgos.clone()).unwrap_or_default();
        let rb: Vec<String> = self.patrones.get(b).map(|p| p.rasgos.clone()).unwrap_or_default();
        let rc: Vec<String> = self.patrones.get(c).map(|p| p.rasgos.clone()).unwrap_or_default();
        let comunes_ab: Vec<&String> = ra.iter().filter(|r| rb.contains(r)).collect();
        if comunes_ab.is_empty() { return None; }
        let mut mejor_d = None; let mut mejor_score = 0.0f32;
        for (nd, pd) in &self.patrones {
            if [a,b,c].contains(&nd.as_str()) { continue; }
            let comunes_cd: Vec<&String> = rc.iter().filter(|r| pd.rasgos.contains(r)).collect();
            let overlap = comunes_ab.iter().filter(|r| comunes_cd.contains(r)).count();
            let score = overlap as f32 / comunes_ab.len().max(1) as f32;
            if score > mejor_score && score > 0.3 { mejor_score = score; mejor_d = Some(nd.clone()); }
        }
        if let Some(d) = mejor_d {
            let a = format!("{} es a {} como {} es a {} ({:.2})", a, b, c, d, mejor_score);
            println!("🔁 [ANALOGÍA] {}", a);
            self.analogias.push(a.clone());
            return Some(a);
        }
        None
    }

    pub fn resolver_problema_nuevo(&mut self, problema: &str, contexto: &[String]) -> bool {
        let mut mejor_p = None; let mut mejor_s = 0.0f32;
        for (n, p) in &self.patrones {
            let s = p.reconocer(contexto);
            if s > mejor_s && s > 0.3 { mejor_s = s; mejor_p = Some(n.clone()); }
        }
        if let Some(p) = mejor_p {
            let sol = format!("Usé patrón '{}' ({:.2})", p, mejor_s);
            println!("✅ [INTEL.GENERAL] {}", sol);
            self.problemas_resueltos.push((problema.to_string(), sol));
            true
        } else {
            self.problemas_fallidos.push(problema.to_string());
            false
        }
    }

    fn a_vector(concepto: &str, rasgos: &[String]) -> Vec<f32> {
        let mut v = vec![0.0f32; 50];
        for (i,c) in concepto.chars().enumerate().take(25) {
            v[i] = if (c as u32) % 2 == 0 { 1.0 } else { -1.0 };
        }
        for (i,r) in rasgos.iter().enumerate().take(25) {
            let h: u32 = r.chars().map(|c| c as u32).sum();
            v[25+i] = if h % 2 == 0 { 1.0 } else { -1.0 };
        }
        v
    }

    pub fn estado(&self) {
        println!("  🎯 INTELIGENCIA GENERAL — 8 componentes");
        println!("   Patrones abstractos:  {}", self.patrones.len());
        println!("   One-shot (Hopfield):  {}/{}", self.memoria_hopfield.n_patrones(), self.memoria_hopfield.capacidad);
        println!("   Transferencias:       {}", self.transferencias.len());
        println!("   Transfer cruzadas:    {}", self.transferencias_cruzadas.len());
        println!("   Analogías:            {}", self.analogias.len());
        println!("   Resueltos:            {}", self.problemas_resueltos.len());
        println!("   Vocabulario activo:   {}", self.linguistica.vocabulario_activo.len());
        println!("   Reglas lógicas:       {}", self.logica_matematica.reglas_inferidas.len());
        println!("   Mapa espacial:        {} dominios", self.espacial.mapa_dominios.len());
        println!("   Patrones temporales:  {}", self.musical.patrones_temporales.len());
        println!("   Habilidades:          {}", self.corporal.habilidades.len());
        println!("   Modelos de otros:     {}", self.interpersonal.modelos_de_otros.len());
        println!("   Taxonomía:            {:?}", self.naturalista.taxonomia.keys().collect::<Vec<_>>());
        println!("   Fortalezas:           {:?}", self.intrapersonal.fortalezas);
        println!("   Estado mental:        {}", self.intrapersonal.estado_mental);
    }
}
