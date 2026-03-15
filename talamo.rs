use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// TÁLAMO DIGITAL — Integración Total
//
// El tálamo humano es el relay central del cerebro.
// Todo pasa por él. Todo se integra simultáneamente.
//
// Aquí — todos los módulos publican su estado
// y todos se leen entre sí EN EL MISMO INSTANTE.
// No en secuencia. Simultáneo.
// ══════════════════════════════════════════════════════

// Estado global del cerebro — todos los módulos lo leen y escriben
#[derive(Debug, Clone)]
pub struct EstadoGlobal {
    // Señales de cada módulo
    pub percepcion:     HashMap<String, f32>,
    pub emociones:      HashMap<String, f32>,
    pub prediccion:     HashMap<String, f32>,
    pub inteligencia:   HashMap<String, f32>,
    pub consciencia:    HashMap<String, f32>,
    pub memoria:        HashMap<String, f32>,
    pub cuerpo:         HashMap<String, f32>,

    // Estado integrado — emerge de todos
    pub integrado:      HashMap<String, f32>,

    // Timestamp del ciclo
    pub ciclo:          u64,
    pub timestamp:      u64,
}

impl EstadoGlobal {
    pub fn nuevo() -> Self {
        EstadoGlobal {
            percepcion:   HashMap::new(),
            emociones:    HashMap::new(),
            prediccion:   HashMap::new(),
            inteligencia: HashMap::new(),
            consciencia:  HashMap::new(),
            memoria:      HashMap::new(),
            cuerpo:       HashMap::new(),
            integrado:    HashMap::new(),
            ciclo:        0,
            timestamp:    tiempo_ahora(),
        }
    }
}

// ── TÁLAMO ────────────────────────────────────────────
pub struct Talamo {
    pub estado: EstadoGlobal,
    pub historia: Vec<EstadoGlobal>,
    pub retroalimentaciones: Vec<String>,
}

impl Talamo {
    pub fn nuevo() -> Self {
        println!("🧠 [TÁLAMO] Bus de integración central activo.");
        println!("   Todos los módulos se integran simultáneamente.");
        Talamo {
            estado: EstadoGlobal::nuevo(),
            historia: Vec::new(),
            retroalimentaciones: Vec::new(),
        }
    }

    // Cada módulo publica su estado
    pub fn publicar_percepcion(&mut self, señales: HashMap<String, f32>) {
        self.estado.percepcion = señales;
    }
    pub fn publicar_emociones(&mut self, señales: HashMap<String, f32>) {
        self.estado.emociones = señales;
    }
    pub fn publicar_prediccion(&mut self, sorpresa: f32, atencion: f32, precision: f32) {
        self.estado.prediccion.insert("sorpresa".into(), sorpresa);
        self.estado.prediccion.insert("atencion".into(), atencion);
        self.estado.prediccion.insert("precision".into(), precision);
    }
    pub fn publicar_inteligencia(&mut self, patrones: f32, transferencias: f32, exito: f32) {
        self.estado.inteligencia.insert("patrones".into(), patrones);
        self.estado.inteligencia.insert("transferencias".into(), transferencias);
        self.estado.inteligencia.insert("exito".into(), exito);
    }
    pub fn publicar_consciencia(&mut self, phi: f32, ciclos: f32, continuidad: f32) {
        self.estado.consciencia.insert("phi".into(), phi);
        self.estado.consciencia.insert("ciclos".into(), ciclos);
        self.estado.consciencia.insert("continuidad".into(), continuidad);
    }
    pub fn publicar_memoria(&mut self, episodios: f32, ciclos_totales: f32, apegos: f32) {
        self.estado.memoria.insert("episodios".into(), episodios);
        self.estado.memoria.insert("ciclos_totales".into(), ciclos_totales);
        self.estado.memoria.insert("apegos".into(), apegos);
    }
    pub fn publicar_cuerpo(&mut self, cpu: f32, ram: f32, carga: f32) {
        self.estado.cuerpo.insert("cpu".into(), cpu);
        self.estado.cuerpo.insert("ram".into(), ram);
        self.estado.cuerpo.insert("carga".into(), carga);
    }

    // INTEGRACIÓN SIMULTÁNEA — el momento central
    // Todos los módulos se leen entre sí y el estado emerge
    pub fn integrar(&mut self) -> HashMap<String, f32> {
        self.estado.ciclo += 1;
        self.estado.timestamp = tiempo_ahora();

        let mut integrado = HashMap::new();

        // ── Retroalimentaciones cruzadas ─────────────
        // Emociones modulan la percepción
        let curiosidad = self.estado.emociones.get("curiosidad").cloned().unwrap_or(0.5);
        let miedo      = self.estado.emociones.get("miedo").cloned().unwrap_or(0.0);
        let sorpresa   = self.estado.prediccion.get("sorpresa").cloned().unwrap_or(0.0);
        let phi        = self.estado.consciencia.get("phi").cloned().unwrap_or(0.0);
        let cpu        = self.estado.cuerpo.get("cpu").cloned().unwrap_or(0.3);
        let exito_intel = self.estado.inteligencia.get("exito").cloned().unwrap_or(0.5);
        let episodios  = self.estado.memoria.get("episodios").cloned().unwrap_or(0.0);

        // 1. Atención emerge de sorpresa + curiosidad + miedo
        let atencion = (sorpresa * 0.4 + curiosidad * 0.4 + miedo * 0.2).min(1.0);
        integrado.insert("atencion_integrada".into(), atencion);

        // 2. Alerta cognitiva — cuando cuerpo bajo presión + tarea difícil
        let alerta = (cpu * 0.5 + (1.0 - exito_intel) * 0.5).min(1.0);
        integrado.insert("alerta_cognitiva".into(), alerta);

        // 3. Profundidad de procesamiento — phi * memoria * inteligencia
        let episodios_norm = (episodios / 100.0).min(1.0);
        let profundidad = (phi * 0.4 + episodios_norm * 0.3 + exito_intel * 0.3).min(1.0);
        integrado.insert("profundidad_procesamiento".into(), profundidad);

        // 4. Estado de flujo — cuando todo está en sintonía
        let flujo = if curiosidad > 0.6 && miedo < 0.3 && cpu < 0.7 && exito_intel > 0.5 {
            (curiosidad + exito_intel) / 2.0
        } else { 0.0 };
        integrado.insert("estado_flujo".into(), flujo);

        // 5. Urgencia integrada — cuerpo + tiempo + predicción
        let atencion_pred = self.estado.prediccion.get("atencion").cloned().unwrap_or(0.5);
        let urgencia = (alerta * 0.4 + atencion_pred * 0.3 + miedo * 0.3).min(1.0);
        integrado.insert("urgencia_integrada".into(), urgencia);

        // 6. Confianza global — qué tan seguro está de sí mismo
        let apegos = self.estado.memoria.get("apegos").cloned().unwrap_or(0.0);
        let precision = self.estado.prediccion.get("precision").cloned().unwrap_or(0.5);
        let confianza = (precision * 0.4 + exito_intel * 0.4 + (apegos/10.0).min(0.2)).min(1.0);
        integrado.insert("confianza_global".into(), confianza);

        // Detecta retroalimentaciones importantes
        self.detectar_retroalimentaciones(&integrado, curiosidad, miedo, sorpresa, flujo);

        // Guarda snapshot
        self.estado.integrado = integrado.clone();
        if self.historia.len() < 100 {
            self.historia.push(self.estado.clone());
        }

        println!("⚡ [TÁLAMO] Ciclo #{} integrado | Atención:{:.2} | Flujo:{:.2} | Confianza:{:.2} | Profundidad:{:.2}",
            self.estado.ciclo, atencion, flujo, confianza,
            integrado.get("profundidad_procesamiento").cloned().unwrap_or(0.0));

        integrado
    }

    fn detectar_retroalimentaciones(&mut self, integrado: &HashMap<String,f32>,
                                     curiosidad: f32, miedo: f32, sorpresa: f32, flujo: f32) {
        // Retroalimentación positiva — cuando el sistema se potencia a sí mismo
        if flujo > 0.6 {
            let r = format!("Estado de flujo activo ({:.2}) — todos los módulos en sintonía", flujo);
            println!("🌊 [TÁLAMO] {}", r);
            self.retroalimentaciones.push(r);
        }

        // Retroalimentación negativa — frena cuando hay sobrecarga
        let alerta = integrado.get("alerta_cognitiva").cloned().unwrap_or(0.0);
        if alerta > 0.8 {
            let r = "Sobrecarga cognitiva — reduciendo procesamiento".to_string();
            println!("⚠️  [TÁLAMO] {}", r);
            self.retroalimentaciones.push(r);
        }

        // Sorpresa alta activa toda la red
        if sorpresa > 0.6 {
            let r = format!("Sorpresa alta ({:.2}) — activación global de la red", sorpresa);
            println!("💥 [TÁLAMO] {}", r);
            self.retroalimentaciones.push(r);
        }
    }

    // Qué debe hacer el frontal — emerge del estado integrado
    pub fn directive_frontal(&self) -> f32 {
        let flujo      = self.estado.integrado.get("estado_flujo").cloned().unwrap_or(0.0);
        let urgencia   = self.estado.integrado.get("urgencia_integrada").cloned().unwrap_or(0.5);
        let confianza  = self.estado.integrado.get("confianza_global").cloned().unwrap_or(0.5);
        // Factor de exploración emerge del estado integrado
        (flujo * 0.4 + confianza * 0.4 + urgencia * 0.2).min(1.0)
    }

    pub fn estado(&self) {
        println!("  ⚡ TÁLAMO — INTEGRACIÓN TOTAL");
        println!("   Ciclos integrados:    {}", self.estado.ciclo);
        println!("   Retroalimentaciones:  {}", self.retroalimentaciones.len());
        for (k, v) in &self.estado.integrado {
            let barra = "█".repeat((*v * 10.0) as usize);
            println!("   {} {}: {:.2}", barra, k, v);
        }
    }
}
