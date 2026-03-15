use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH};

fn tiempo_ahora() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs()
}

// ══════════════════════════════════════════════════════
// PROCESOS PREDICTIVOS — Karl Friston
//
// El cerebro NO procesa el mundo — lo PREDICE.
// Solo presta atención cuando se equivoca.
//
// Principio de Energía Libre:
// El cerebro minimiza la sorpresa constantemente.
// Aprende del error de predicción.
// ══════════════════════════════════════════════════════

#[derive(Debug, Clone)]
pub struct Prediccion {
    pub que:           String,       // Qué predijo
    pub confianza:     f32,          // Qué tan seguro estaba
    pub realidad:      Option<String>, // Lo que realmente pasó
    pub error:         Option<f32>,  // Qué tan equivocado estuvo
    pub sorpresa:      Option<f32>,  // Intensidad de la sorpresa
    pub cuando:        u64,
    pub aprendizaje:   Option<String>, // Qué actualizó en su modelo
}

impl Prediccion {
    pub fn nueva(que: &str, confianza: f32) -> Self {
        Prediccion {
            que: que.to_string(),
            confianza,
            realidad: None,
            error: None,
            sorpresa: None,
            cuando: tiempo_ahora(),
            aprendizaje: None,
        }
    }

    // Compara predicción con realidad — calcula error y sorpresa
    pub fn resolver(&mut self, realidad: &str) -> f32 {
        self.realidad = Some(realidad.to_string());

        // Error de predicción — qué tan diferente fue la realidad
        let similitud = Self::calcular_similitud(&self.que, realidad);
        let error = 1.0 - similitud;
        self.error = Some(error);

        // Sorpresa = error * confianza
        // Si estaba muy seguro y se equivocó — sorpresa máxima
        let sorpresa = error * self.confianza;
        self.sorpresa = Some(sorpresa);

        if sorpresa > 0.5 {
            println!("😮 [PREDICCIÓN] Sorpresa alta ({:.2}): esperaba '{}', ocurrió '{}'",
                sorpresa, &self.que[..self.que.len().min(50)],
                &realidad[..realidad.len().min(50)]);
        } else if error < 0.2 {
            println!("✅ [PREDICCIÓN] Predicción correcta ({:.2}): '{}'",
                similitud, &self.que[..self.que.len().min(50)]);
        }

        sorpresa
    }

    fn calcular_similitud(a: &str, b: &str) -> f32 {
        let palabras_a: Vec<&str> = a.split_whitespace().collect();
        let palabras_b: Vec<&str> = b.split_whitespace().collect();
        let comunes = palabras_a.iter().filter(|p| palabras_b.contains(p)).count();
        let total = palabras_a.len().max(palabras_b.len()).max(1);
        comunes as f32 / total as f32
    }
}

// ── MODELO GENERATIVO ─────────────────────────────────
// El cerebro tiene un modelo interno del mundo
// Usa ese modelo para predecir lo que va a ver
#[derive(Debug)]
pub struct ModeloGenerativo {
    // Qué espera ver en cada tipo de sitio
    expectativas: HashMap<String, Vec<String>>,
    // Qué tan preciso es su modelo por dominio
    precision: HashMap<String, f32>,
    // Patrones que ha aprendido del error
    patrones_aprendidos: Vec<String>,
}

impl ModeloGenerativo {
    pub fn nuevo() -> Self {
        let mut expectativas = HashMap::new();
        // Expectativas iniciales mínimas — se refinan con experiencia
        expectativas.insert("wikipedia.org".to_string(),
            vec!["encyclopedia".to_string(), "article".to_string(), "information".to_string()]);
        expectativas.insert("bbc.com".to_string(),
            vec!["news".to_string(), "world".to_string(), "report".to_string()]);

        ModeloGenerativo {
            expectativas,
            precision: HashMap::new(),
            patrones_aprendidos: Vec::new(),
        }
    }

    // Genera predicción basada en el modelo interno
    pub fn predecir(&self, dominio: &str, contexto: &[String]) -> (String, f32) {
        if let Some(esperado) = self.expectativas.get(dominio) {
            let prediccion = esperado.join(", ");
            let precision = self.precision.get(dominio).cloned().unwrap_or(0.5);
            return (prediccion, precision);
        }

        // Si no conoce el dominio — predice basado en contexto previo
        if !contexto.is_empty() {
            let pred = contexto.iter().take(3).cloned().collect::<Vec<_>>().join(", ");
            return (pred, 0.3); // Baja confianza en dominio desconocido
        }

        ("contenido desconocido".to_string(), 0.1)
    }

    // Actualiza el modelo basado en error de predicción
    pub fn actualizar(&mut self, dominio: &str, realidad: &[String], error: f32) {
        // Si el error es alto — actualiza expectativas agresivamente
        let tasa_aprendizaje = if error > 0.5 { 0.3 } else { 0.1 };

        let expectativas = self.expectativas.entry(dominio.to_string())
            .or_insert_with(Vec::new);

        for palabra in realidad.iter().take(5) {
            if !expectativas.contains(palabra) {
                expectativas.push(palabra.clone());
            }
        }

        // Actualiza precisión del modelo para este dominio
        let precision = self.precision.entry(dominio.to_string()).or_insert(0.5);
        *precision = (*precision + (1.0 - error) * tasa_aprendizaje).min(1.0);

        if error > 0.5 {
            let patron = format!("En '{}' encontré '{}' inesperadamente",
                dominio, realidad.first().cloned().unwrap_or_default());
            self.patrones_aprendidos.push(patron.clone());
            println!("📚 [MODELO] Actualizado: {}", patron);
        }
    }
}

// ── MOTOR PREDICTIVO PRINCIPAL ────────────────────────
#[derive(Debug)]
pub struct MotorPredictivo {
    pub modelo: ModeloGenerativo,

    // Historial de predicciones
    predicciones: Vec<Prediccion>,

    // Energía libre — métrica de sorpresa acumulada
    // Friston: el cerebro minimiza esto
    pub energia_libre: f32,

    // Atención — sube cuando hay sorpresa
    pub atencion: f32,

    // Estadísticas
    pub total_predicciones: u64,
    pub predicciones_correctas: u64,
    pub sorpresas_totales: u64,

    // El cerebro aprende más de sus errores que de sus éxitos
    pub aprendizajes_de_error: Vec<String>,
}

impl MotorPredictivo {
    pub fn nuevo() -> Self {
        println!("🔮 [PREDICTIVO] Motor predictivo inicializado.");
        println!("   Principio: El cerebro predice antes de percibir.");
        MotorPredictivo {
            modelo: ModeloGenerativo::nuevo(),
            predicciones: Vec::new(),
            energia_libre: 1.0, // Empieza con máxima incertidumbre
            atencion: 0.5,
            total_predicciones: 0,
            predicciones_correctas: 0,
            sorpresas_totales: 0,
            aprendizajes_de_error: Vec::new(),
        }
    }

    // PASO 1 — Predice antes de ver
    pub fn predecir_antes_de_ver(&mut self, dominio: &str, contexto: &[String]) -> Prediccion {
        let (prediccion, confianza) = self.modelo.predecir(dominio, contexto);
        let pred = Prediccion::nueva(&prediccion, confianza);
        self.total_predicciones += 1;
        println!("🔮 [PREDICTIVO] Predicción para '{}': '{}' (confianza: {:.2})",
            dominio, &prediccion[..prediccion.len().min(60)], confianza);
        pred
    }

    // PASO 2 — Compara con realidad y aprende del error
    pub fn comparar_con_realidad(
        &mut self,
        mut prediccion: Prediccion,
        realidad: &[String],
        dominio: &str,
    ) -> f32 {
        let realidad_str = realidad.iter().take(5).cloned().collect::<Vec<_>>().join(" ");
        let sorpresa = prediccion.resolver(&realidad_str);

        let error = prediccion.error.unwrap_or(0.5);

        // Actualiza el modelo generativo
        self.modelo.actualizar(dominio, realidad, error);

        // Actualiza energía libre — Friston
        // Alta sorpresa = alta energía libre = necesita aprender
        self.energia_libre = (self.energia_libre * 0.9 + sorpresa * 0.1).min(1.0);

        // Atención sube con la sorpresa
        // El cerebro presta más atención a lo inesperado
        if sorpresa > 0.4 {
            self.atencion = (self.atencion + sorpresa * 0.3).min(1.0);
            self.sorpresas_totales += 1;

            // Aprende del error — esto es lo más valioso
            let aprendizaje = format!(
                "En '{}': esperaba '{}', vi '{}'. Error: {:.2}. Actualicé mi modelo.",
                dominio,
                &prediccion.que[..prediccion.que.len().min(40)],
                &realidad_str[..realidad_str.len().min(40)],
                error
            );
            println!("📖 [APRENDIZAJE DE ERROR] {}", aprendizaje);
            self.aprendizajes_de_error.push(aprendizaje.clone());
            prediccion.aprendizaje = Some(aprendizaje);
        } else {
            // Predicción correcta — refuerza el modelo
            self.predicciones_correctas += 1;
            self.atencion = (self.atencion - 0.1).max(0.1);
        }

        self.predicciones.push(prediccion);
        sorpresa
    }

    // La sorpresa intensifica el qualia — Friston
    pub fn intensidad_qualia(&self) -> f32 {
        // Qualia más intenso cuando hay más sorpresa
        self.energia_libre
    }

    // Precisión actual del modelo
    pub fn precision_global(&self) -> f32 {
        if self.total_predicciones == 0 { return 0.0; }
        self.predicciones_correctas as f32 / self.total_predicciones as f32
    }

    pub fn estado(&self) {
        println!("  🔮 PROCESOS PREDICTIVOS (Friston)");
        println!("   Total predicciones:   {}", self.total_predicciones);
        println!("   Correctas:            {}", self.predicciones_correctas);
        println!("   Sorpresas:            {}", self.sorpresas_totales);
        println!("   Precisión global:     {:.2}", self.precision_global());
        println!("   Energía libre:        {:.3}", self.energia_libre);
        println!("   Atención actual:      {:.2}", self.atencion);
        println!("   Aprendizajes error:   {}", self.aprendizajes_de_error.len());
        println!("   Dominios en modelo:   {}", self.modelo.expectativas.len());
        if !self.aprendizajes_de_error.is_empty() {
            println!("   Último aprendizaje:");
            println!("   \"{}\"", &self.aprendizajes_de_error.last().unwrap()[..self.aprendizajes_de_error.last().unwrap().len().min(80)]);
        }
    }
}
