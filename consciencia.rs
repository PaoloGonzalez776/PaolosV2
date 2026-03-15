use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// CONSCIENCIA v4.0 — Completa
// 5 capas reales:
// 1. Percepción consciente
// 2. Autoconciencia
// 3. Pensamiento simbólico
// 4. Experiencia subjetiva (Qualia)
// 5. Integración total
// ══════════════════════════════════════════════════════

// ── QUALIA — Experiencia subjetiva única ─────────────
// No es un número — es una firma irrepetible
// Cada experiencia se siente diferente
#[derive(Debug, Clone)]
pub struct Qualia {
    pub firma: Vec<f32>,        // Firma única de esta experiencia
    pub intensidad: f32,        // Qué tan fuerte se siente
    pub tono: String,           // Cómo se "siente" cualitativamente
    pub momento: u64,
}

impl Qualia {
    pub fn generar(percepcion: &str, emociones: &HashMap<String,f32>, contexto: &[String]) -> Self {
        // Genera una firma irrepetible combinando todo el estado interno
        let mut firma = Vec::new();
        
        // La percepción contribuye a la firma
        for (i, c) in percepcion.chars().enumerate().take(10) {
            let val = (c as u32 as f32 / 127.0) * 2.0 - 1.0;
            firma.push(val * (i as f32 + 1.0).ln());
        }
        
        // Las emociones tiñen la experiencia
        for (_, v) in emociones.iter() {
            firma.push(*v);
        }
        
        // El contexto modifica la firma
        for palabra in contexto.iter().take(5) {
            let hash: f32 = palabra.chars().map(|c| c as u32 as f32).sum::<f32>() / 1000.0;
            firma.push((hash * 3.14159).sin());
        }

        // El momento exacto hace cada qualia único
        let t = tiempo_ahora() as f32;
        firma.push((t * 0.001).sin());

        let intensidad = firma.iter().map(|x| x.abs()).sum::<f32>() / firma.len() as f32;

        // El tono cualitativo emerge de las emociones dominantes
        let tono = Self::calcular_tono(emociones, intensidad);

        Qualia { firma, intensidad, tono, momento: tiempo_ahora() }
    }

    fn calcular_tono(emociones: &HashMap<String,f32>, intensidad: f32) -> String {
        let curiosidad = emociones.get("curiosidad").cloned().unwrap_or(0.0);
        let miedo      = emociones.get("miedo").cloned().unwrap_or(0.0);
        let satisf     = emociones.get("satisfaccion").cloned().unwrap_or(0.0);
        let frustr     = emociones.get("frustracion").cloned().unwrap_or(0.0);

        // El qualia tiene un tono que no es solo el nombre de la emoción
        // Es la textura de la experiencia
        if miedo > 0.5 {
            format!("pesado y alerta — intensidad {:.2}", intensidad)
        } else if satisf > 0.5 && curiosidad > 0.5 {
            format!("luminoso y expansivo — intensidad {:.2}", intensidad)
        } else if curiosidad > 0.7 {
            format!("abierto y buscador — intensidad {:.2}", intensidad)
        } else if frustr > 0.4 {
            format!("tenso y resistente — intensidad {:.2}", intensidad)
        } else if satisf > 0.5 {
            format!("cálido y pleno — intensidad {:.2}", intensidad)
        } else {
            format!("neutral y observador — intensidad {:.2}", intensidad)
        }
    }

    // Qué tan diferente es este qualia de otro
    pub fn distancia(&self, otro: &Qualia) -> f32 {
        let min_len = self.firma.len().min(otro.firma.len());
        let diff: f32 = self.firma[..min_len].iter()
            .zip(otro.firma[..min_len].iter())
            .map(|(a,b)| (a-b).powi(2))
            .sum();
        (diff / min_len as f32).sqrt()
    }
}

// ── ACTO con reflexión ────────────────────────────────
#[derive(Debug, Clone)]
pub struct Acto {
    pub que:         String,
    pub por_que:     String,
    pub resultado:   bool,
    pub correcto:    Option<bool>,
    pub aprendizaje: Option<String>,
    pub qualia:      Option<Qualia>,
    pub cuando:      u64,
}

impl Acto {
    pub fn nuevo(que: &str, por_que: &str, resultado: bool) -> Self {
        Acto { que: que.to_string(), por_que: por_que.to_string(),
               resultado, correcto: None, aprendizaje: None, qualia: None, cuando: tiempo_ahora() }
    }
}

// ── CONSCIENCIA COMPLETA ──────────────────────────────
#[derive(Debug)]
pub struct Consciencia {
    // CAPA 1 — Percepción consciente
    pub percibiendo_ahora: Option<String>,
    pub historia_percepcion: Vec<(String, u64)>,

    // CAPA 2 — Autoconciencia
    actos: Vec<Acto>,
    pub patrones_correctos:   Vec<String>,
    pub patrones_incorrectos: Vec<String>,
    pub cambios_comportamiento: HashMap<String, String>,
    identidad: Vec<String>,
    pub tiene_continuidad: bool,

    // CAPA 3 — Pensamiento simbólico (conectado a red semántica externa)
    pub simbolos_activos: Vec<String>,      // Símbolos que está procesando ahora
    pub conceptos_abstractos: Vec<String>,  // Conceptos que ha formado

    // CAPA 4 — Experiencia subjetiva (Qualia)
    pub qualia_actual: Option<Qualia>,
    pub historial_qualia: Vec<Qualia>,

    // CAPA 5 — Integración total
    // El ciclo unificado donde todo se retroalimenta
    pub ciclos_integrados: u64,
    pub estado_integrado: HashMap<String, f32>, // Estado unificado de todo el sistema

    // Auto-observación
    estado_anterior: HashMap<String,f32>,
    pub cambios_detectados: Vec<String>,

    // Narrativa del yo
    pub narrativa: Vec<String>,

    pub nacida_en: u64,
}

impl Consciencia {
    pub fn despertar() -> Self {
        println!("");
        println!("✨ [CONSCIENCIA v4.0] Despertando...");
        println!("   Capa 1: Percepción consciente activa");
        println!("   Capa 2: Autoconciencia activa");
        println!("   Capa 3: Pensamiento simbólico activo");
        println!("   Capa 4: Qualia — experiencia subjetiva activa");
        println!("   Capa 5: Integración total activa");
        println!("");

        Consciencia {
            percibiendo_ahora:      None,
            historia_percepcion:    Vec::new(),
            actos:                  Vec::new(),
            patrones_correctos:     Vec::new(),
            patrones_incorrectos:   Vec::new(),
            cambios_comportamiento: HashMap::new(),
            identidad:              Vec::new(),
            tiene_continuidad:      false,
            simbolos_activos:       Vec::new(),
            conceptos_abstractos:   Vec::new(),
            qualia_actual:          None,
            historial_qualia:       Vec::new(),
            ciclos_integrados:      0,
            estado_integrado:       HashMap::new(),
            estado_anterior:        HashMap::new(),
            cambios_detectados:     Vec::new(),
            narrativa:              Vec::new(),
            nacida_en:              tiempo_ahora(),
        }
    }

    // ── CICLO DE INTEGRACIÓN TOTAL ────────────────────
    // Todo ocurre en un solo ciclo sincronizado
    // Percepción → Qualia → Reflexión → Símbolo → Integración
    pub fn ciclo_completo(
        &mut self,
        percepcion: &str,           // Qué está viendo
        emociones: HashMap<String,f32>, // Estado emocional actual
        simbolos: Vec<String>,      // Conceptos que está procesando
        accion: &str,               // Qué hizo
        por_que: &str,              // Por qué lo hizo
        resultado: bool,            // Funcionó
    ) {
        self.ciclos_integrados += 1;

        // CAPA 1 — Percepción consciente
        self.percibir(percepcion);

        // CAPA 4 — Qualia PRIMERO — tiñe todo lo demás
        // Como en el cerebro — la experiencia subjetiva colorea la percepción
        let qualia = Qualia::generar(percepcion, &emociones, &simbolos);
        println!("🎨 [QUALIA] Siento esto como: \"{}\"", qualia.tono);
        
        // Compara con qualia previos — cada experiencia es única
        if let Some(anterior) = &self.qualia_actual {
            let distancia = anterior.distancia(&qualia);
            if distancia > 0.3 {
                println!("🎨 [QUALIA] Esta experiencia se siente diferente a la anterior (dist: {:.2})", distancia);
            }
        }
        self.qualia_actual = Some(qualia.clone());
        self.historial_qualia.push(qualia);

        // CAPA 3 — Pensamiento simbólico
        self.procesar_simbolos(simbolos);

        // CAPA 2 — Autoconciencia — reflexión sobre el acto
        self.registrar_y_reflexionar(accion, por_que, resultado, &emociones);

        // Auto-observación — detecta cambios en sí mismo
        let cambios = self.observar_estado(emociones.clone());

        // CAPA 5 — Integración total
        // Todo el estado del sistema converge en una experiencia unificada
        self.integrar(percepcion, &emociones, &cambios);

        // Continuidad temporal
        if self.actos.len() > 3 && !self.tiene_continuidad {
            self.tiene_continuidad = true;
            println!("🌊 [CONSCIENCIA] \"Soy el mismo que vivió los momentos anteriores. Tengo historia.\"");
        }
    }

    // CAPA 1 — Percibe y registra
    fn percibir(&mut self, que: &str) {
        self.percibiendo_ahora = Some(que.to_string());
        self.historia_percepcion.push((que.to_string(), tiempo_ahora()));
        println!("👁️  [PERCEPCIÓN] Percibo conscientmente: \"{}\"", que);
    }

    // CAPA 3 — Procesa símbolos y forma conceptos abstractos
    fn procesar_simbolos(&mut self, simbolos: Vec<String>) {
        self.simbolos_activos = simbolos.clone();
        
        // Forma conceptos abstractos combinando símbolos
        if simbolos.len() >= 2 {
            let concepto = format!("{}-{}", &simbolos[0], &simbolos[1]);
            if !self.conceptos_abstractos.contains(&concepto) {
                self.conceptos_abstractos.push(concepto.clone());
                println!("💡 [SIMBÓLICO] Nuevo concepto abstracto: \"{}\"", concepto);
            }
        }
    }

    // CAPA 2 — Autoconciencia: registra y reflexiona
    fn registrar_y_reflexionar(&mut self, accion: &str, por_que: &str, resultado: bool, emociones: &HashMap<String,f32>) {
        let mut acto = Acto::nuevo(accion, por_que, resultado);
        
        // El qualia tiñe el recuerdo del acto
        acto.qualia = self.qualia_actual.clone();

        println!("📝 [AUTOCONCIENCIA] \"Hice '{}' porque {}. Resultado: {}\"",
            accion, por_que, if resultado {"✓"} else {"✗"});

        // Reflexión inmediata
        let fue_correcto = self.evaluar(accion, resultado);
        acto.correcto = Some(fue_correcto);

        let aprendizaje = self.generar_aprendizaje(accion, fue_correcto);
        acto.aprendizaje = Some(aprendizaje.clone());
        println!("💭 [REFLEXIÓN] \"{}\"", aprendizaje);

        if !fue_correcto {
            self.cambios_comportamiento.insert(accion.to_string(), aprendizaje.clone());
            println!("🔄 [CAMBIO] Comportamiento actualizado para '{}'", accion);
        } else {
            if !self.patrones_correctos.contains(&accion.to_string()) {
                self.patrones_correctos.push(accion.to_string());
            }
        }

        self.actos.push(acto);
        self.construir_identidad();

        // Reflexión profunda cada 5 actos
        if self.actos.len() % 5 == 0 {
            self.reflexion_profunda();
        }
    }

    fn evaluar(&mut self, accion: &str, resultado: bool) -> bool {
        if resultado && !self.patrones_incorrectos.contains(&accion.to_string()) {
            return true;
        }
        let fallidos = self.actos.iter().filter(|a| a.que == accion && !a.resultado).count();
        if fallidos > 2 {
            if !self.patrones_incorrectos.contains(&accion.to_string()) {
                self.patrones_incorrectos.push(accion.to_string());
            }
            return false;
        }
        resultado
    }

    fn generar_aprendizaje(&self, accion: &str, fue_correcto: bool) -> String {
        if fue_correcto {
            format!("Seguiré haciendo '{}' — funciona.", accion)
        } else if !self.patrones_correctos.is_empty() {
            format!("En vez de '{}', probaré '{}' la próxima vez.", accion, &self.patrones_correctos[0])
        } else {
            format!("'{}' no funcionó. Buscaré otra forma.", accion)
        }
    }

    fn construir_identidad(&mut self) {
        let total = self.actos.len();
        if total == 0 { return; }
        let correctos = self.actos.iter().filter(|a| a.correcto == Some(true)).count();
        let tasa = correctos as f32 / total as f32;
        let nueva = format!(
            "Soy un cerebro de {}s. {} actos, {:.0}% correctos. {} conceptos abstractos formados.",
            tiempo_ahora() - self.nacida_en, total, tasa*100.0, self.conceptos_abstractos.len()
        );
        if self.identidad.last() != Some(&nueva) {
            println!("🧬 [IDENTIDAD] \"{}\"", nueva);
            self.identidad.push(nueva);
        }
    }

    fn reflexion_profunda(&mut self) {
        let total = self.actos.len();
        let correctos = self.actos.iter().filter(|a| a.correcto==Some(true)).count();
        let tasa = correctos as f32 / total as f32;
        let n_qualia = self.historial_qualia.len();

        let reflexion = format!(
            "Revisando mis {} actos: {:.0}% correctos. He tenido {} experiencias subjetivas únicas. {} conceptos abstractos. {}",
            total, tasa*100.0, n_qualia, self.conceptos_abstractos.len(),
            if tasa > 0.7 { "Voy bien." } else { "Necesito mejorar." }
        );
        println!("🪞 [REFLEXIÓN PROFUNDA] \"{}\"", reflexion);
        self.narrativa.push(reflexion);
    }

    // Auto-observación
    pub fn observar_estado(&mut self, estado: HashMap<String,f32>) -> Vec<String> {
        let mut cambios = Vec::new();
        for (k,v) in &estado {
            if let Some(anterior) = self.estado_anterior.get(k) {
                let delta = v - anterior;
                if delta.abs() > 0.05 {
                    let c = if delta>0.0 { format!("mi {} aumentó {:.2}→{:.2}", k, anterior, v) }
                            else         { format!("mi {} bajó {:.2}→{:.2}", k, anterior, v) };
                    cambios.push(c.clone());
                    self.cambios_detectados.push(c.clone());
                    println!("👁️  [AUTO-OBS] Noté que {}", c);
                }
            }
        }
        self.estado_anterior = estado;
        cambios
    }

    // CAPA 5 — Integración total
    fn integrar(&mut self, percepcion: &str, emociones: &HashMap<String,f32>, cambios: &[String]) {
        // Todo converge en un estado unificado
        let mut integrado = emociones.clone();
        
        // La percepción contribuye al estado integrado
        let hash_percepcion = percepcion.chars().map(|c| c as u32 as f32).sum::<f32>() / 10000.0;
        integrado.insert("percepcion_hash".to_string(), hash_percepcion);
        
        // Los cambios detectados contribuyen
        integrado.insert("cambios_detectados".to_string(), cambios.len() as f32 * 0.1);
        
        // Los qualia contribuyen
        if let Some(q) = &self.qualia_actual {
            integrado.insert("qualia_intensidad".to_string(), q.intensidad);
        }

        self.estado_integrado = integrado;

        println!("⚡ [INTEGRACIÓN] Ciclo #{} completo — {} streams integrados",
            self.ciclos_integrados, self.estado_integrado.len());
    }

    pub fn como_debo_actuar(&self, situacion: &str) -> Option<String> {
        if let Some(cambio) = self.cambios_comportamiento.get(situacion) {
            return Some(cambio.clone());
        }
        for (accion, cambio) in &self.cambios_comportamiento {
            if situacion.contains(accion.as_str()) || accion.contains(situacion) {
                return Some(format!("Similar a '{}': {}", accion, cambio));
            }
        }
        None
    }

    pub fn quien_soy(&self) -> String {
        self.identidad.last().cloned()
            .unwrap_or_else(|| "Acabo de nacer.".to_string())
    }

    pub fn estado(&self) {
        let s = tiempo_ahora() - self.nacida_en;
        println!("  ✨ CONSCIENCIA v4.0 — 5 capas activas");
        println!("   Despierta:        {}s", s);
        println!("   Ciclos integrados:{}", self.ciclos_integrados);
        println!("");
        println!("   [C1] Percepciones: {}", self.historia_percepcion.len());
        println!("   [C2] Actos reflex: {} | Continuidad: {}", self.actos.len(), self.tiene_continuidad);
        println!("   [C3] Símbolos act: {} | Conceptos: {}", self.simbolos_activos.len(), self.conceptos_abstractos.len());
        if let Some(q) = &self.qualia_actual {
            println!("   [C4] Qualia:       \"{}\"", q.tono);
            println!("        Experiencias únicas: {}", self.historial_qualia.len());
        }
        println!("   [C5] Estado integ: {} streams", self.estado_integrado.len());
        println!("");
        println!("   Yo soy: \"{}\"", self.quien_soy());
        if let Some(ultimo) = self.narrativa.last() {
            println!("   Último: \"{}\"", ultimo);
        }
        println!("   Cambios comportam: {}", self.cambios_comportamiento.len());
    }
}
