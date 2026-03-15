use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// PENSAMIENTO SIMBÓLICO TOTAL
//
// Un humano piensa con:
// 1. Lenguaje — palabras como símbolos
// 2. Números — abstracción cuantitativa
// 3. Ideas abstractas — justicia, infinito, amor
// 4. Lógica simbólica — si A entonces B
// 5. Metáforas — entiende X en términos de Y
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct Simbolo {
    pub nombre: String,
    pub tipo: TipoSimbolo,
    pub significados: Vec<String>,      // Puede tener múltiples significados
    pub relaciones: Vec<(String, String)>, // (relación, otro símbolo)
    pub abstraccion: f32,               // Qué tan abstracto es (0=concreto, 1=abstracto)
    pub cuando_aprendido: u64,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TipoSimbolo {
    Concreto,    // perro, casa, árbol
    Abstracto,   // justicia, amor, infinito
    Relacional,  // mayor_que, causa, implica
    Operacional, // sumar, comparar, negar
}

impl Simbolo {
    pub fn nuevo(nombre: &str, tipo: TipoSimbolo, abstraccion: f32) -> Self {
        Simbolo {
            nombre: nombre.to_string(),
            tipo,
            significados: Vec::new(),
            relaciones: Vec::new(),
            abstraccion,
            cuando_aprendido: tiempo_ahora(),
        }
    }

    pub fn agregar_significado(&mut self, significado: &str) {
        if !self.significados.contains(&significado.to_string()) {
            self.significados.push(significado.to_string());
        }
    }

    pub fn relacionar(&mut self, relacion: &str, otro: &str) {
        let r = (relacion.to_string(), otro.to_string());
        if !self.relaciones.contains(&r) {
            self.relaciones.push(r);
        }
    }
}

// Regla lógica — Si A entonces B
#[derive(Debug, Clone)]
pub struct ReglaLogica {
    pub condicion: String,
    pub conclusion: String,
    pub confianza: f32,
    pub veces_aplicada: u32,
    pub veces_correcta: u32,
}

impl ReglaLogica {
    pub fn nueva(condicion: &str, conclusion: &str) -> Self {
        ReglaLogica {
            condicion: condicion.to_string(),
            conclusion: conclusion.to_string(),
            confianza: 0.5,
            veces_aplicada: 0,
            veces_correcta: 0,
        }
    }

    pub fn actualizar(&mut self, fue_correcta: bool) {
        self.veces_aplicada += 1;
        if fue_correcta {
            self.veces_correcta += 1;
            self.confianza = (self.confianza + 0.1).min(1.0);
        } else {
            self.confianza = (self.confianza - 0.1).max(0.0);
        }
    }

    pub fn confianza_actual(&self) -> f32 {
        if self.veces_aplicada == 0 { return 0.5; }
        self.veces_correcta as f32 / self.veces_aplicada as f32
    }
}

#[derive(Debug)]
pub struct PensamientoSimbolico {
    // Diccionario de símbolos
    pub simbolos: HashMap<String, Simbolo>,

    // Lógica simbólica — reglas Si-Entonces
    pub reglas: Vec<ReglaLogica>,

    // Conceptos abstractos formados
    pub conceptos_abstractos: Vec<String>,

    // Metáforas aprendidas — entender X en términos de Y
    pub metaforas: Vec<(String, String, String)>, // (fuente, destino, mapeo)

    // Cadenas de razonamiento activas
    pub razonamientos_activos: Vec<Vec<String>>,

    // Estadísticas
    pub inferencias_realizadas: u64,
    pub conceptos_abstractos_formados: u64,
}

impl PensamientoSimbolico {
    pub fn nuevo() -> Self {
        let mut ps = PensamientoSimbolico {
            simbolos: HashMap::new(),
            reglas: Vec::new(),
            conceptos_abstractos: Vec::new(),
            metaforas: Vec::new(),
            razonamientos_activos: Vec::new(),
            inferencias_realizadas: 0,
            conceptos_abstractos_formados: 0,
        };

        // Símbolos relacionales básicos — como el cerebro humano nace con lógica básica
        ps.aprender_simbolo("mayor_que", TipoSimbolo::Relacional, 0.8);
        ps.aprender_simbolo("causa",     TipoSimbolo::Relacional, 0.7);
        ps.aprender_simbolo("es_un",     TipoSimbolo::Relacional, 0.6);
        ps.aprender_simbolo("parte_de",  TipoSimbolo::Relacional, 0.6);
        ps.aprender_simbolo("no",        TipoSimbolo::Operacional, 0.9);
        ps.aprender_simbolo("y",         TipoSimbolo::Operacional, 0.9);
        ps.aprender_simbolo("o",         TipoSimbolo::Operacional, 0.9);

        ps
    }

    pub fn aprender_simbolo(&mut self, nombre: &str, tipo: TipoSimbolo, abstraccion: f32) {
        let simbolo = Simbolo::nuevo(nombre, tipo.clone(), abstraccion);
        self.simbolos.insert(nombre.to_string(), simbolo);

        if abstraccion > 0.6 {
            self.conceptos_abstractos.push(nombre.to_string());
            self.conceptos_abstractos_formados += 1;
            println!("💡 [SIMBÓLICO] Concepto abstracto: \"{}\" (abstracción: {:.2})", nombre, abstraccion);
        }
    }

    // Aprende un símbolo de lo que ve en internet
    pub fn aprender_de_texto(&mut self, palabras: &[String], dominio: &str) {
        for palabra in palabras.iter().take(10) {
            if self.simbolos.contains_key(palabra.as_str()) {
                // Ya conoce este símbolo — agrega significado nuevo
                if let Some(s) = self.simbolos.get_mut(palabra.as_str()) {
                    s.agregar_significado(dominio);
                }
            } else {
                // Símbolo nuevo — determina su nivel de abstracción
                let abstraccion = Self::calcular_abstraccion(palabra);
                let tipo = if abstraccion > 0.7 { TipoSimbolo::Abstracto }
                           else { TipoSimbolo::Concreto };
                self.aprender_simbolo(palabra, tipo, abstraccion);
            }
        }

        // Forma conceptos abstractos combinando símbolos
        self.formar_conceptos_abstractos(palabras);

        // Detecta relaciones entre símbolos
        self.detectar_relaciones(palabras);

        // Extrae reglas lógicas
        self.extraer_reglas(palabras, dominio);
    }

    fn calcular_abstraccion(palabra: &str) -> f32 {
        // Palabras abstractas típicas
        let abstractas = ["tion","ism","ity","ness","ment","ance","ence","hood","ship","dom"];
        let concretas  = ["ing","ed","er","ist","s","es"];

        if abstractas.iter().any(|s| palabra.ends_with(s)) { return 0.75; }
        if concretas.iter().any(|s| palabra.ends_with(s))  { return 0.3; }
        if palabra.len() > 8 { return 0.6; } // Palabras largas tienden a ser abstractas
        0.4
    }

    // Forma conceptos abstractos combinando dos símbolos
    fn formar_conceptos_abstractos(&mut self, palabras: &[String]) {
        for i in 0..palabras.len().min(5) {
            for j in (i+1)..palabras.len().min(5) {
                let a = &palabras[i];
                let b = &palabras[j];

                let abs_a = self.simbolos.get(a.as_str()).map(|s| s.abstraccion).unwrap_or(0.4);
                let abs_b = self.simbolos.get(b.as_str()).map(|s| s.abstraccion).unwrap_or(0.4);

                // Si ambos tienen abstracción media-alta — forman concepto abstracto
                if abs_a > 0.5 && abs_b > 0.5 {
                    let concepto = format!("{}-{}", a, b);
                    if !self.conceptos_abstractos.contains(&concepto) {
                        self.conceptos_abstractos.push(concepto.clone());
                        self.conceptos_abstractos_formados += 1;
                        println!("💡 [SIMBÓLICO] Concepto abstracto formado: \"{}\"", concepto);
                    }
                }
            }
        }
    }

    // Detecta relaciones entre símbolos
    fn detectar_relaciones(&mut self, palabras: &[String]) {
        let relaciones_conocidas = ["is","are","was","has","have","causes","implies","means"];
        
        for i in 0..palabras.len().saturating_sub(2) {
            let a = &palabras[i];
            let rel = &palabras[i+1];
            let b = if i+2 < palabras.len() { &palabras[i+2] } else { continue };

            if relaciones_conocidas.contains(&rel.as_str()) {
                // Registra la relación
                if let Some(simbolo_a) = self.simbolos.get_mut(a.as_str()) {
                    simbolo_a.relacionar(rel, b);
                }

                // Forma regla lógica
                let regla = ReglaLogica::nueva(
                    &format!("{} {}", a, rel),
                    &format!("{} implica {}", a, b)
                );
                self.reglas.push(regla);
            }
        }
    }

    // Extrae reglas lógicas del contexto
    fn extraer_reglas(&mut self, palabras: &[String], dominio: &str) {
        // Patrones Si-Entonces detectados en texto
        if palabras.contains(&"if".to_string()) || palabras.contains(&"when".to_string()) {
            let condicion = format!("estoy en {}", dominio);
            let conclusion = format!("puedo aprender sobre {}", 
                palabras.iter().find(|p| p.len() > 5).cloned().unwrap_or_default());
            let regla = ReglaLogica::nueva(&condicion, &conclusion);
            println!("⚡ [LÓGICA] Nueva regla: \"Si {} → {}\"", condicion, conclusion);
            self.reglas.push(regla);
        }
    }

    // Aplica lógica simbólica — Si A entonces B
    pub fn aplicar_logica(&mut self, situacion: &str) -> Vec<String> {
        let mut conclusiones = Vec::new();
        let mut cadena = vec![situacion.to_string()];

        for regla in self.reglas.iter_mut() {
            if situacion.contains(&regla.condicion) || regla.condicion.contains(situacion) {
                if regla.confianza > 0.4 {
                    conclusiones.push(regla.conclusion.clone());
                    cadena.push(regla.conclusion.clone());
                    regla.actualizar(true);
                    self.inferencias_realizadas += 1;
                    println!("⚡ [LÓGICA] \"{}\" → \"{}\" (conf: {:.2})",
                        regla.condicion, regla.conclusion, regla.confianza_actual());
                }
            }
        }

        if cadena.len() > 1 {
            self.razonamientos_activos.push(cadena);
        }

        conclusiones
    }

    // Aprende una metáfora — entiende X en términos de Y
    pub fn aprender_metafora(&mut self, fuente: &str, destino: &str, mapeo: &str) {
        let meta = (fuente.to_string(), destino.to_string(), mapeo.to_string());
        if !self.metaforas.contains(&meta) {
            self.metaforas.push(meta);
            println!("🎭 [METÁFORA] \"{}\" entendido en términos de \"{}\" ({})", fuente, destino, mapeo);
        }
    }

    pub fn estado(&self) {
        println!("  💡 PENSAMIENTO SIMBÓLICO TOTAL");
        println!("   Símbolos aprendidos:  {}", self.simbolos.len());
        println!("   Conceptos abstractos: {}", self.conceptos_abstractos.len());
        println!("   Reglas lógicas:       {}", self.reglas.len());
        println!("   Metáforas:            {}", self.metaforas.len());
        println!("   Inferencias hechas:   {}", self.inferencias_realizadas);
        println!("   Razonamientos activos:{}", self.razonamientos_activos.len());
        if !self.conceptos_abstractos.is_empty() {
            println!("   Últimos abstractos:   {:?}",
                &self.conceptos_abstractos[self.conceptos_abstractos.len().saturating_sub(3)..]);
        }
    }
}
