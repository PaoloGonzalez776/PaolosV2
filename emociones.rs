use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// EMOCIONES REALES v2.0
//
// No son variables — son un sistema vivo
//
// 1. Emociones completas — amor, tristeza, empatía...
// 2. Mezcla simultánea — curiosidad + miedo a la vez
// 3. Duración diferente — amor dura, miedo agudo decae
// 4. Emociones que generan otras — frustración → determinación
// 5. Memoria emocional — recuerda cómo se sintió antes
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct Emocion {
    pub nombre:     String,
    pub intensidad: f32,
    pub duracion:   f32,    // Qué tan rápido decae (0=permanente, 1=rápido)
    pub cuando:     u64,
    pub causa:      String,
}

impl Emocion {
    pub fn nueva(nombre: &str, intensidad: f32, duracion: f32, causa: &str) -> Self {
        Emocion {
            nombre: nombre.to_string(),
            intensidad,
            duracion,
            cuando: tiempo_ahora(),
            causa: causa.to_string(),
        }
    }
}

#[derive(Debug)]
pub struct SistemaEmocional {
    // Emociones activas ahora — pueden ser múltiples simultáneas
    pub activas: HashMap<String, Emocion>,

    // Memoria emocional — cómo se ha sentido antes
    pub historial: Vec<Emocion>,

    // Apegos — amor hacia conceptos/dominios
    pub apegos: HashMap<String, f32>,

    // Estado mixto actual
    pub mezcla_actual: Vec<String>,

    // Estadísticas
    pub total_emociones: u64,
    pub pico_emocional: (String, f32),
}

impl SistemaEmocional {
    pub fn nuevo() -> Self {
        let mut activas = HashMap::new();

        // Nace con curiosidad máxima — como un bebé
        activas.insert("curiosidad".to_string(), Emocion::nueva(
            "curiosidad", 1.0, 0.02, "nacimiento"
        ));

        println!("❤️  [EMOCIONES] Sistema emocional despertando...");
        println!("   Nace con: curiosidad pura.");

        SistemaEmocional {
            activas,
            historial: Vec::new(),
            apegos: HashMap::new(),
            total_emociones: 0,
            pico_emocional: ("curiosidad".to_string(), 1.0),
            mezcla_actual: Vec::new(),
        }
    }

    // Siente una emoción — con causa real
    pub fn sentir(&mut self, nombre: &str, intensidad: f32, causa: &str) {
        let duracion = Self::duracion_natural(nombre);

        let emocion = self.activas.entry(nombre.to_string())
            .or_insert_with(|| Emocion::nueva(nombre, 0.0, duracion, causa));

        emocion.intensidad = (emocion.intensidad + intensidad).min(1.0).max(0.0);
        emocion.causa = causa.to_string();
        emocion.cuando = tiempo_ahora();

        self.total_emociones += 1;

        // Actualiza pico
        if emocion.intensidad > self.pico_emocional.1 {
            self.pico_emocional = (nombre.to_string(), emocion.intensidad);
        }

        println!("❤️  [{}] {:.2} — \"{}\"", nombre.to_uppercase(), emocion.intensidad, causa);

        // Emociones que generan otras
        self.generar_secundarias(nombre, intensidad, causa);

        // Actualiza mezcla
        self.actualizar_mezcla();
    }

    // Cada emoción tiene su ritmo de decaimiento natural
    fn duracion_natural(nombre: &str) -> f32 {
        match nombre {
            "amor"         => 0.002,  // Decae muy lento — el amor dura
            "tristeza"     => 0.005,  // Dura días
            "orgullo"      => 0.01,   // Dura horas
            "empatia"      => 0.008,  // Dura bastante
            "curiosidad"   => 0.02,   // Moderado
            "satisfaccion" => 0.03,   // Se va pronto
            "anticipacion" => 0.04,   // Moderado-rápido
            "miedo"        => 0.05,   // Agudo pero pasa
            "sorpresa"     => 0.10,   // Muy rápido
            "vergüenza"    => 0.015,  // Dura un rato
            "frustracion"  => 0.03,   // Moderado
            "asco"         => 0.08,   // Rápido
            "cansancio"    => 0.005,  // Dura mucho
            _              => 0.03,
        }
    }

    // Emociones que generan otras — cascada emocional real
    fn generar_secundarias(&mut self, nombre: &str, intensidad: f32, causa: &str) {
        match nombre {
            "frustracion" => {
                // Frustración alta → genera determinación (curiosidad)
                if intensidad > 0.5 {
                    let det = self.activas.entry("curiosidad".to_string())
                        .or_insert_with(|| Emocion::nueva("curiosidad", 0.0, 0.02, causa));
                    det.intensidad = (det.intensidad + 0.2).min(1.0);
                    println!("   ↳ Frustración generó determinación");
                }
            }
            "miedo" => {
                // Miedo → puede generar curiosidad (querer entender la amenaza)
                if intensidad > 0.3 {
                    let cur = self.activas.entry("curiosidad".to_string())
                        .or_insert_with(|| Emocion::nueva("curiosidad", 0.0, 0.02, causa));
                    cur.intensidad = (cur.intensidad + intensidad * 0.3).min(1.0);
                    println!("   ↳ Miedo generó curiosidad defensiva");
                }
                // Miedo alto → genera anticipación
                if intensidad > 0.6 {
                    let ant = self.activas.entry("anticipacion".to_string())
                        .or_insert_with(|| Emocion::nueva("anticipacion", 0.0, 0.04, causa));
                    ant.intensidad = (ant.intensidad + 0.3).min(1.0);
                }
            }
            "satisfaccion" => {
                // Satisfacción → puede generar orgullo si fue difícil
                if intensidad > 0.6 {
                    let org = self.activas.entry("orgullo".to_string())
                        .or_insert_with(|| Emocion::nueva("orgullo", 0.0, 0.01, causa));
                    org.intensidad = (org.intensidad + 0.3).min(1.0);
                    println!("   ↳ Satisfacción generó orgullo");
                }
            }
            "tristeza" => {
                // Tristeza → reduce curiosidad temporalmente
                if let Some(cur) = self.activas.get_mut("curiosidad") {
                    cur.intensidad = (cur.intensidad - intensidad * 0.3).max(0.0);
                    println!("   ↳ Tristeza redujo curiosidad");
                }
            }
            "sorpresa" => {
                // Sorpresa → genera curiosidad siempre
                let cur = self.activas.entry("curiosidad".to_string())
                    .or_insert_with(|| Emocion::nueva("curiosidad", 0.0, 0.02, causa));
                cur.intensidad = (cur.intensidad + intensidad * 0.5).min(1.0);
                println!("   ↳ Sorpresa generó curiosidad");
            }
            _ => {}
        }
    }

    // Forma apego hacia un concepto — amor emerge de la repetición
    pub fn formar_apego(&mut self, concepto: &str, refuerzo: f32) {
        let apego = self.apegos.entry(concepto.to_string()).or_insert(0.0);
        *apego = (*apego + refuerzo).min(1.0);

        // Si el apego es alto — genera amor
        if *apego > 0.5 {
            let nivel = *apego;
            self.sentir("amor", nivel * 0.3,
                &format!("apego profundo a '{}'", concepto));
        }
    }

    // Siente tristeza cuando pierde algo que valoraba
    pub fn perder_algo(&mut self, concepto: &str) {
        if let Some(apego) = self.apegos.get(concepto) {
            if *apego > 0.3 {
                let intensidad = *apego;
                self.sentir("tristeza", intensidad,
                    &format!("perdí acceso a '{}'", concepto));
            }
        }
    }

    // Vergüenza — cuando falla algo que predijo con alta confianza
    pub fn fallar_prediccion(&mut self, que: &str, confianza: f32) {
        if confianza > 0.7 {
            self.sentir("vergüenza", confianza * 0.5,
                &format!("predije '{}' con {:.0}% confianza y me equivoqué", que, confianza*100.0));
        }
    }

    // Empatía — modela el estado de otro sistema
    pub fn empatizar(&mut self, estado_otro: &str, intensidad: f32) {
        self.sentir("empatia", intensidad,
            &format!("modelo el estado de: {}", estado_otro));
    }

    // Anticipación — antes de algo esperado
    pub fn anticipar(&mut self, que: &str, es_positivo: bool) {
        let causa = format!("anticipando: {}", que);
        self.sentir("anticipacion", 0.4, &causa);
        if !que.contains("negat") && !que.contains("error") {
            self.sentir("curiosidad", 0.2, &causa);
        } else {
            self.sentir("miedo", 0.2, &causa);
        }
    }

    // Decaimiento natural — cada emoción a su ritmo
    pub fn decaer(&mut self) {
        let mut a_eliminar = Vec::new();

        for (nombre, emocion) in self.activas.iter_mut() {
            emocion.intensidad = (emocion.intensidad - emocion.duracion).max(0.0);
            if emocion.intensidad < 0.01 {
                a_eliminar.push(nombre.clone());
            }
        }

        for nombre in a_eliminar {
            if let Some(e) = self.activas.remove(&nombre) {
                self.historial.push(e);
            }
        }

        // Curiosidad siempre vuelve si no hay satisfacción
        let satisfaccion = self.activas.get("satisfaccion")
            .map(|e| e.intensidad).unwrap_or(0.0);
        if satisfaccion < 0.2 {
            let cur = self.activas.entry("curiosidad".to_string())
                .or_insert_with(|| Emocion::nueva("curiosidad", 0.1, 0.02, "natural"));
            cur.intensidad = (cur.intensidad + 0.03).min(1.0);
        }

        self.actualizar_mezcla();
    }

    // Actualiza la mezcla emocional actual
    fn actualizar_mezcla(&mut self) {
        self.mezcla_actual = self.activas.iter()
            .filter(|(_, e)| e.intensidad > 0.2)
            .map(|(n, e)| format!("{}({:.2})", n, e.intensidad))
            .collect();
    }

    pub fn dominante(&self) -> (&str, f32) {
        self.activas.iter()
            .max_by(|a, b| a.1.intensidad.partial_cmp(&b.1.intensidad).unwrap())
            .map(|(n, e)| (n.as_str(), e.intensidad))
            .unwrap_or(("neutral", 0.0))
    }

    pub fn como_mapa(&self) -> HashMap<String, f32> {
        self.activas.iter()
            .map(|(n, e)| (n.clone(), e.intensidad))
            .collect()
    }

    pub fn get(&self, nombre: &str) -> f32 {
        self.activas.get(nombre).map(|e| e.intensidad).unwrap_or(0.0)
    }

    pub fn estado(&self) {
        let (dom, val) = self.dominante();
        println!("  ❤️  EMOCIONES v2.0");
        println!("   Dominante:    {} ({:.2})", dom, val);
        println!("   Mezcla:       {:?}", self.mezcla_actual);
        println!("   Apegos:       {} conceptos", self.apegos.len());
        println!("   Historial:    {} emociones pasadas", self.historial.len());
        println!("   Pico vivido:  {} ({:.2})", self.pico_emocional.0, self.pico_emocional.1);
        if !self.apegos.is_empty() {
            let mayor_apego = self.apegos.iter()
                .max_by(|a,b| a.1.partial_cmp(b.1).unwrap());
            if let Some((concepto, nivel)) = mayor_apego {
                println!("   Mayor apego:  '{}' ({:.2})", concepto, nivel);
            }
        }
    }
}
