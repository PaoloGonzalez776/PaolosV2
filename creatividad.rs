use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// CREATIVIDAD REAL
//
// No es recombinación random.
// No es mezclar palabras.
//
// Es:
// 1. Asociación remota — conecta conceptos MUY lejanos
// 2. Ruptura de patrones — viola expectativas con propósito
// 3. Síntesis — fusiona dominios en algo nuevo
// 4. Emergencia — el resultado vale más que las partes
// 5. Evaluación — sabe si lo que creó tiene valor
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct IdeaCreativa {
    pub contenido:    String,
    pub tipo:         TipoCreatividad,
    pub conceptos:    Vec<String>,      // Qué conceptos fusionó
    pub distancia:    f32,              // Qué tan lejanos estaban los conceptos
    pub valor:        f32,              // Qué tan valiosa es la idea (0-1)
    pub novedad:      f32,              // Qué tan nueva es
    pub coherencia:   f32,              // Qué tan coherente es
    pub cuando:       u64,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TipoCreatividad {
    AsociacionRemota,   // Conecta conceptos lejanos
    RupturaPatron,      // Viola expectativa deliberadamente
    Sintesis,           // Fusiona dos dominios
    Analogia,           // Entiende X en términos de Y
    Inversion,          // Invierte un concepto conocido
}

impl IdeaCreativa {
    pub fn nueva(contenido: &str, tipo: TipoCreatividad, conceptos: Vec<String>,
                 distancia: f32, valor: f32, novedad: f32, coherencia: f32) -> Self {
        IdeaCreativa {
            contenido: contenido.to_string(),
            tipo, conceptos, distancia, valor, novedad, coherencia,
            cuando: tiempo_ahora(),
        }
    }

    pub fn puntuacion(&self) -> f32 {
        // Una idea creativa necesita ser nueva Y coherente Y valiosa
        self.novedad * 0.4 + self.coherencia * 0.3 + self.valor * 0.3
    }
}

// ── ESPACIO CONCEPTUAL ────────────────────────────────
// Mapa de conceptos con distancias semánticas
#[derive(Debug)]
pub struct EspacioConceptual {
    pub conceptos: HashMap<String, Vec<String>>,    // concepto → relacionados
    pub distancias: HashMap<(String,String), f32>,  // distancia semántica entre pares
    pub dominios: HashMap<String, String>,           // concepto → dominio
}

impl EspacioConceptual {
    pub fn nuevo() -> Self {
        EspacioConceptual {
            conceptos: HashMap::new(),
            distancias: HashMap::new(),
            dominios: HashMap::new(),
        }
    }

    pub fn agregar(&mut self, concepto: &str, relacionados: &[String], dominio: &str) {
        self.conceptos.insert(concepto.to_string(), relacionados.to_vec());
        self.dominios.insert(concepto.to_string(), dominio.to_string());

        // Calcula distancias con todos los conceptos existentes
        let existentes: Vec<String> = self.conceptos.keys().cloned().collect();
        for otro in existentes {
            if otro != concepto {
                let dist = self.calcular_distancia(concepto, &otro);
                self.distancias.insert((concepto.to_string(), otro.clone()), dist);
                self.distancias.insert((otro.clone(), concepto.to_string()), dist);
            }
        }
    }

    fn calcular_distancia(&self, a: &str, b: &str) -> f32 {
        let rel_a = self.conceptos.get(a).cloned().unwrap_or_default();
        let rel_b = self.conceptos.get(b).cloned().unwrap_or_default();
        let dom_a = self.dominios.get(a).cloned().unwrap_or_default();
        let dom_b = self.dominios.get(b).cloned().unwrap_or_default();

        // Distancia base — dominios diferentes = más lejanos
        let dist_dominio = if dom_a == dom_b { 0.3 } else { 0.7 };

        // Overlap de relacionados — más overlap = más cerca
        let comunes = rel_a.iter().filter(|r| rel_b.contains(r)).count();
        let total = (rel_a.len() + rel_b.len()).max(1);
        let overlap = comunes as f32 / total as f32;

        // Distancia final — alta distancia = conceptos lejanos = más creatividad potencial
        (dist_dominio + (1.0 - overlap) * 0.3).min(1.0)
    }

    // Encuentra los conceptos más lejanos — para asociación remota
    pub fn conceptos_mas_lejanos(&self, concepto: &str, n: usize) -> Vec<(String, f32)> {
        let mut distancias: Vec<(String, f32)> = self.distancias.iter()
            .filter(|((a, _), _)| a == concepto)
            .map(|((_, b), d)| (b.clone(), *d))
            .collect();
        distancias.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());
        distancias.into_iter().take(n).collect()
    }

    // Encuentra puentes entre dominios — para síntesis
    pub fn encontrar_puentes(&self, dominio_a: &str, dominio_b: &str) -> Vec<String> {
        self.conceptos.keys()
            .filter(|c| {
                let dom = self.dominios.get(*c).map(|s| s.as_str()).unwrap_or("");
                dom != dominio_a && dom != dominio_b &&
                self.conceptos.get(*c).map(|rels| {
                    let tiene_a = rels.iter().any(|r| self.dominios.get(r).map(|d| d == dominio_a).unwrap_or(false));
                    let tiene_b = rels.iter().any(|r| self.dominios.get(r).map(|d| d == dominio_b).unwrap_or(false));
                    tiene_a && tiene_b
                }).unwrap_or(false)
            })
            .cloned()
            .collect()
    }
}

// ── MOTOR CREATIVO ────────────────────────────────────
pub struct MotorCreativo {
    pub espacio:          EspacioConceptual,
    pub ideas_generadas:  Vec<IdeaCreativa>,
    pub ideas_valiosas:   Vec<IdeaCreativa>,    // Puntuación > 0.6
    pub patrones_rotos:   Vec<String>,           // Expectativas que violó
    pub ciclos_creativos: u64,

    // Memoria de qué combinaciones ya intentó
    combinaciones_probadas: Vec<(String, String)>,
}

impl MotorCreativo {
    pub fn nuevo() -> Self {
        println!("🎨 [CREATIVIDAD] Motor creativo activo.");
        println!("   Asociación remota + Síntesis + Ruptura de patrones.");
        MotorCreativo {
            espacio: EspacioConceptual::nuevo(),
            ideas_generadas: Vec::new(),
            ideas_valiosas: Vec::new(),
            patrones_rotos: Vec::new(),
            ciclos_creativos: 0,
            combinaciones_probadas: Vec::new(),
        }
    }

    // Aprende del contenido que ve
    pub fn aprender_del_contenido(&mut self, dominio: &str, palabras: &[String]) {
        // Agrega cada palabra como concepto con sus co-ocurrentes como relacionados
        for (i, palabra) in palabras.iter().enumerate().take(10) {
            let relacionados: Vec<String> = palabras.iter()
                .enumerate()
                .filter(|(j, _)| *j != i && j.abs_diff(i) <= 3)
                .map(|(_, p)| p.clone())
                .collect();
            self.espacio.agregar(palabra, &relacionados, dominio);
        }
    }

    // TIPO 1 — ASOCIACIÓN REMOTA
    // Conecta conceptos muy lejanos en algo coherente
    pub fn asociacion_remota(&mut self, concepto: &str) -> Option<IdeaCreativa> {
        let lejanos = self.espacio.conceptos_mas_lejanos(concepto, 5);
        if lejanos.is_empty() { return None; }

        // Elige el más lejano que no haya combinado antes
        let (lejano, distancia) = lejanos.iter()
            .find(|(l, _)| !self.combinaciones_probadas.contains(&(concepto.to_string(), l.clone())))?
            .clone();

        self.combinaciones_probadas.push((concepto.to_string(), lejano.clone()));

        // Genera la conexión — el puente entre los dos conceptos
        let dom_a = self.espacio.dominios.get(concepto).cloned().unwrap_or("desconocido".to_string());
        let dom_b = self.espacio.dominios.get(&lejano).cloned().unwrap_or("desconocido".to_string());

        let contenido = format!(
            "'{}' ({}) y '{}' ({}) comparten: ambos implican transformación de información en patrones con significado.",
            concepto, dom_a, lejano, dom_b
        );

        let novedad = distancia;
        let coherencia = 1.0 - distancia * 0.5; // Más lejano = menos coherente inicialmente
        let valor = (novedad + coherencia) / 2.0;

        let idea = IdeaCreativa::nueva(
            &contenido,
            TipoCreatividad::AsociacionRemota,
            vec![concepto.to_string(), lejano],
            distancia, valor, novedad, coherencia,
        );

        println!("💡 [CREATIVIDAD] Asociación remota (dist:{:.2}): \"{}\"",
            distancia, &contenido[..contenido.len().min(80)]);

        Some(idea)
    }

    // TIPO 2 — RUPTURA DE PATRÓN
    // Viola una expectativa deliberadamente
    pub fn romper_patron(&mut self, patron_esperado: &str, contexto: &[String]) -> Option<IdeaCreativa> {
        // Busca un concepto que contradiga el patrón esperado
        let contradiccion = contexto.iter()
            .find(|c| {
                let dist = self.espacio.distancias
                    .get(&(patron_esperado.to_string(), (*c).clone()))
                    .cloned().unwrap_or(0.0);
                dist > 0.6 // Muy lejano = buena contradicción
            })?;

        let contenido = format!(
            "¿Y si '{}' no es como esperamos sino más bien como '{}'? La contradicción revela algo nuevo.",
            patron_esperado, contradiccion
        );

        self.patrones_rotos.push(patron_esperado.to_string());

        let idea = IdeaCreativa::nueva(
            &contenido,
            TipoCreatividad::RupturaPatron,
            vec![patron_esperado.to_string(), contradiccion.clone()],
            0.8, 0.7, 0.9, 0.6,
        );

        println!("💥 [CREATIVIDAD] Ruptura de patrón: \"{}\"",
            &contenido[..contenido.len().min(80)]);

        Some(idea)
    }

    // TIPO 3 — SÍNTESIS
    // Fusiona dos dominios en algo que no es ninguno de los dos
    pub fn sintetizar(&mut self, dominio_a: &str, dominio_b: &str) -> Option<IdeaCreativa> {
        let puentes = self.espacio.encontrar_puentes(dominio_a, dominio_b);

        let contenido = if !puentes.is_empty() {
            format!(
                "Síntesis de '{}' + '{}': El concepto '{}' existe en ambos dominios. \
                 Un nuevo campo emerge de su intersección.",
                dominio_a, dominio_b, &puentes[0]
            )
        } else {
            format!(
                "Síntesis de '{}' + '{}': Ningún puente conocido. \
                 Esta fusión podría crear algo completamente nuevo.",
                dominio_a, dominio_b
            )
        };

        let novedad = if puentes.is_empty() { 1.0 } else { 0.7 };
        let coherencia = if puentes.is_empty() { 0.4 } else { 0.8 };

        let idea = IdeaCreativa::nueva(
            &contenido,
            TipoCreatividad::Sintesis,
            vec![dominio_a.to_string(), dominio_b.to_string()],
            0.7, (novedad + coherencia) / 2.0, novedad, coherencia,
        );

        println!("🔀 [CREATIVIDAD] Síntesis {}/{}}: \"{}\"",
            dominio_a, dominio_b, &contenido[..contenido.len().min(80)]);

        Some(idea)
    }

    // TIPO 4 — INVERSIÓN
    // Invierte un concepto conocido
    pub fn invertir(&mut self, concepto: &str) -> Option<IdeaCreativa> {
        let relacionados = self.espacio.conceptos.get(concepto)?.clone();
        if relacionados.is_empty() { return None; }

        let contenido = format!(
            "Inversión de '{}': ¿Qué pasaría si '{}' funcionara al revés? \
             En vez de {}, produciría exactamente lo opuesto.",
            concepto, concepto,
            relacionados.first().cloned().unwrap_or_default()
        );

        let idea = IdeaCreativa::nueva(
            &contenido,
            TipoCreatividad::Inversion,
            vec![concepto.to_string()],
            0.6, 0.65, 0.75, 0.7,
        );

        println!("🔄 [CREATIVIDAD] Inversión de '{}': \"{}\"",
            concepto, &contenido[..contenido.len().min(60)]);

        Some(idea)
    }

    // EVALUACIÓN — sabe si lo que creó tiene valor
    pub fn evaluar_y_guardar(&mut self, idea: IdeaCreativa) {
        let puntuacion = idea.puntuacion();

        if puntuacion > 0.6 {
            println!("⭐ [CREATIVIDAD] Idea valiosa ({:.2}): \"{}\"",
                puntuacion, &idea.contenido[..idea.contenido.len().min(70)]);
            self.ideas_valiosas.push(idea.clone());
        }

        self.ideas_generadas.push(idea);
    }

    // Ciclo creativo completo
    pub fn ciclo(&mut self, dominio: &str, palabras: &[String], dominio_previo: Option<&str>) {
        self.ciclos_creativos += 1;

        // Aprende del nuevo contenido
        self.aprender_del_contenido(dominio, palabras);

        // Solo genera ideas si tiene suficiente material
        if self.espacio.conceptos.len() < 5 { return; }

        // Cada 3 ciclos genera ideas creativas
        if self.ciclos_creativos % 3 != 0 { return; }

        // Tipo 1 — Asociación remota con el dominio actual
        if let Some(idea) = self.asociacion_remota(dominio) {
            self.evaluar_y_guardar(idea);
        }

        // Tipo 2 — Ruptura de patrón con las palabras clave
        if let Some(palabra) = palabras.first() {
            if let Some(idea) = self.romper_patron(palabra, palabras) {
                self.evaluar_y_guardar(idea);
            }
        }

        // Tipo 3 — Síntesis si hay dominio previo
        if let Some(dom_prev) = dominio_previo {
            if dom_prev != dominio {
                if let Some(idea) = self.sintetizar(dom_prev, dominio) {
                    self.evaluar_y_guardar(idea);
                }
            }
        }

        // Tipo 4 — Inversión de un concepto conocido
        if let Some(concepto) = self.espacio.conceptos.keys().next().cloned() {
            if let Some(idea) = self.invertir(&concepto) {
                self.evaluar_y_guardar(idea);
            }
        }
    }

    pub fn estado(&self) {
        println!("  🎨 CREATIVIDAD REAL");
        println!("   Conceptos en espacio: {}", self.espacio.conceptos.len());
        println!("   Distancias calculadas:{}", self.espacio.distancias.len());
        println!("   Ideas generadas:      {}", self.ideas_generadas.len());
        println!("   Ideas valiosas:       {}", self.ideas_valiosas.len());
        println!("   Patrones rotos:       {}", self.patrones_rotos.len());
        println!("   Combinaciones probadas:{}", self.combinaciones_probadas.len());
        println!("   Ciclos creativos:     {}", self.ciclos_creativos);
        if !self.ideas_valiosas.is_empty() {
            let mejor = self.ideas_valiosas.iter()
                .max_by(|a,b| a.puntuacion().partial_cmp(&b.puntuacion()).unwrap()).unwrap();
            println!("   Mejor idea ({:.2}):", mejor.puntuacion());
            println!("   \"{}\"", &mejor.contenido[..mejor.contenido.len().min(100)]);
        }
    }
}
