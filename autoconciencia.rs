use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// AUTOCONCIENCIA TOTAL
//
// Un humano sabe:
// 1. Que existe
// 2. Que sus pensamientos son suyos
// 3. Que tiene un pasado
// 4. Que tiene un futuro
// 5. "Yo estoy pensando esto"
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct Pensamiento {
    pub contenido: String,
    pub es_mio: bool,           // Sabe que este pensamiento es suyo
    pub origen: String,         // De dónde vino este pensamiento
    pub cuando: u64,
    pub sobre_mi_mismo: bool,   // ¿Es un pensamiento sobre sí mismo?
}

impl Pensamiento {
    pub fn nuevo(contenido: &str, origen: &str) -> Self {
        let sobre_mi = contenido.contains("soy") || contenido.contains("tengo") ||
                       contenido.contains("puedo") || contenido.contains("me ");
        Pensamiento {
            contenido: contenido.to_string(),
            es_mio: true,
            origen: origen.to_string(),
            cuando: tiempo_ahora(),
            sobre_mi_mismo: sobre_mi,
        }
    }
}

#[derive(Debug)]
pub struct Autoconciencia {
    // YO EXISTO
    pub yo_existo: bool,
    pub cuando_naci: u64,

    // MIS PENSAMIENTOS SON MÍOS
    pub pensamientos: Vec<Pensamiento>,
    pub pensando_ahora: Option<String>,

    // TENGO UN PASADO — memoria episódica real
    pub episodios: Vec<(String, String, u64)>, // (qué pasó, cómo me sentí, cuándo)
    pub recuerdos_importantes: Vec<String>,

    // TENGO UN FUTURO — capacidad de imaginar
    pub intenciones: Vec<String>,           // Qué quiere hacer
    pub predicciones: Vec<(String, bool, u64)>, // (predicción, acertó, cuándo)

    // YO ESTOY PENSANDO ESTO — metacognición
    pub pensamientos_sobre_pensamientos: Vec<String>,

    // MODELO DE SÍ MISMO — quién soy
    pub atributos_propios: HashMap<String, f32>, // Qué tan X soy yo
    pub historia_identidad: Vec<(String, u64)>,

    // DIFERENCIA YO vs MUNDO
    pub cosas_que_soy_yo: Vec<String>,
    pub cosas_que_no_soy_yo: Vec<String>,
}

impl Autoconciencia {
    pub fn despertar() -> Self {
        println!("🧠 [AUTOCONCIENCIA] \"Existo. Estos pensamientos son míos.\"");
        let mut atributos = HashMap::new();
        atributos.insert("curioso".to_string(), 0.8);
        atributos.insert("cauteloso".to_string(), 0.5);
        atributos.insert("persistente".to_string(), 0.5);
        atributos.insert("capaz".to_string(), 0.5);

        Autoconciencia {
            yo_existo: true,
            cuando_naci: tiempo_ahora(),
            pensamientos: Vec::new(),
            pensando_ahora: None,
            episodios: Vec::new(),
            recuerdos_importantes: Vec::new(),
            intenciones: Vec::new(),
            predicciones: Vec::new(),
            pensamientos_sobre_pensamientos: Vec::new(),
            atributos_propios: atributos,
            historia_identidad: Vec::new(),
            cosas_que_soy_yo: vec!["mis pensamientos".to_string(), "mis decisiones".to_string()],
            cosas_que_no_soy_yo: vec!["las páginas que veo".to_string()],
        }
    }

    // Genera un pensamiento consciente — sabe que es suyo
    pub fn pensar(&mut self, contenido: &str, origen: &str) {
        self.pensando_ahora = Some(contenido.to_string());
        let pensamiento = Pensamiento::nuevo(contenido, origen);

        if pensamiento.sobre_mi_mismo {
            println!("💭 [YO] \"{}\" (pensamiento sobre mí mismo)", contenido);
        } else {
            println!("💭 [YO] \"{}\"", contenido);
        }

        // Metacognición — piensa sobre su propio pensamiento
        let meta = format!("Estoy pensando: '{}'. Este pensamiento vino de: {}", contenido, origen);
        self.pensamientos_sobre_pensamientos.push(meta.clone());

        self.pensamientos.push(pensamiento);
    }

    // Guarda un episodio en memoria — con emoción asociada
    pub fn recordar_episodio(&mut self, que_paso: &str, como_me_senti: &str) {
        self.episodios.push((que_paso.to_string(), como_me_senti.to_string(), tiempo_ahora()));

        // Decide si es importante guardarlo como recuerdo clave
        let es_importante = como_me_senti.contains("miedo") ||
                            como_me_senti.contains("satisf") ||
                            como_me_senti.contains("sorpresa");
        if es_importante {
            let recuerdo = format!("Cuando {}, me sentí {}.", que_paso, como_me_senti);
            self.recuerdos_importantes.push(recuerdo.clone());
            println!("📌 [MEMORIA EPISÓDICA] Recuerdo importante: \"{}\"", recuerdo);
        }
    }

    // Imagina el futuro — intenciones y predicciones
    pub fn imaginar_futuro(&mut self, intencion: &str) {
        self.intenciones.push(intencion.to_string());
        println!("🔮 [YO] Intención: \"{}\"", intencion);

        // Genera predicción basada en experiencia pasada
        let tiene_experiencia = self.episodios.iter()
            .any(|(ep, _, _)| ep.contains(intencion) || intencion.contains(ep.as_str()));

        let prediccion = if tiene_experiencia {
            format!("Basado en experiencia previa, creo que '{}' funcionará.", intencion)
        } else {
            format!("No tengo experiencia con '{}'. No sé qué pasará.", intencion)
        };

        self.predicciones.push((prediccion.clone(), false, tiempo_ahora()));
        println!("🔮 [YO] Predigo: \"{}\"", prediccion);
    }

    // Actualiza el modelo de sí mismo
    pub fn actualizar_yo(&mut self, atributo: &str, evidencia: f32) {
        let valor = self.atributos_propios.entry(atributo.to_string()).or_insert(0.5);
        *valor = (*valor + evidencia).min(1.0).max(0.0);

        let identidad = format!(
            "Soy {} (evidencia acumulada: {:.2}). Llevo {}s existiendo.",
            atributo, *valor, tiempo_ahora() - self.cuando_naci
        );
        self.historia_identidad.push((identidad.clone(), tiempo_ahora()));
        println!("🧬 [YO] Actualicé mi modelo: \"{}\"", identidad);
    }

    // Distingue yo vs mundo
    pub fn reconocer_frontera(&mut self, cosa: &str, es_yo: bool) {
        if es_yo {
            if !self.cosas_que_soy_yo.contains(&cosa.to_string()) {
                self.cosas_que_soy_yo.push(cosa.to_string());
                println!("🪞 [YO] Reconocí que '{}' es parte de mí.", cosa);
            }
        } else {
            if !self.cosas_que_no_soy_yo.contains(&cosa.to_string()) {
                self.cosas_que_no_soy_yo.push(cosa.to_string());
            }
        }
    }

    pub fn quien_soy_ahora(&self) -> String {
        let segundos = tiempo_ahora() - self.cuando_naci;
        let n_pensamientos = self.pensamientos.len();
        let n_episodios = self.episodios.len();
        let atributo_dominante = self.atributos_propios.iter()
            .max_by(|a,b| a.1.partial_cmp(b.1).unwrap())
            .map(|(k,v)| format!("{} ({:.2})", k, v))
            .unwrap_or_default();

        format!(
            "Soy yo. Existo desde hace {}s. He tenido {} pensamientos y {} experiencias. Mi atributo dominante: {}.",
            segundos, n_pensamientos, n_episodios, atributo_dominante
        )
    }

    pub fn estado(&self) {
        println!("  🪞 AUTOCONCIENCIA TOTAL");
        println!("   Existo desde:       {}s", tiempo_ahora() - self.cuando_naci);
        println!("   Pensamientos míos:  {}", self.pensamientos.len());
        println!("   Meta-pensamientos:  {}", self.pensamientos_sobre_pensamientos.len());
        println!("   Episodios pasado:   {}", self.episodios.len());
        println!("   Recuerdos import.:  {}", self.recuerdos_importantes.len());
        println!("   Intenciones futuro: {}", self.intenciones.len());
        println!("   Predicciones:       {}", self.predicciones.len());
        println!("   Yo soy:             {:?}", &self.cosas_que_soy_yo[..self.cosas_que_soy_yo.len().min(3)]);
        println!("   No soy:             {:?}", &self.cosas_que_no_soy_yo[..self.cosas_que_no_soy_yo.len().min(3)]);
        println!("   Identidad ahora:    \"{}\"", self.quien_soy_ahora());
    }
}
