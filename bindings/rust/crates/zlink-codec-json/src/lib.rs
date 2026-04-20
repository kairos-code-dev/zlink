use std::fmt;

#[derive(Debug)]
pub enum Error {
    Json(serde_json::Error),
    Message(zlink::ConfigError),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Json(err) => write!(f, "json codec failed: {err}"),
            Self::Message(err) => write!(f, "zlink message creation failed: {err}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Json(err) => Some(err),
            Self::Message(err) => Some(err),
        }
    }
}

impl From<serde_json::Error> for Error {
    fn from(value: serde_json::Error) -> Self {
        Self::Json(value)
    }
}

impl From<zlink::ConfigError> for Error {
    fn from(value: zlink::ConfigError) -> Self {
        Self::Message(value)
    }
}

pub fn decode<T: serde::de::DeserializeOwned>(msg: &zlink::Message) -> Result<T, Error> {
    serde_json::from_slice(msg.as_bytes()).map_err(Error::from)
}

pub fn encode<T: serde::Serialize>(v: &T) -> Result<zlink::Message, Error> {
    let bytes = serde_json::to_vec(v)?;
    zlink::Message::from_bytes(&bytes).map_err(Error::from)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::{Deserialize, Serialize};

    #[derive(Debug, PartialEq, Serialize, Deserialize)]
    struct Person {
        name: String,
        age: u32,
    }

    #[test]
    fn roundtrip() {
        let original = Person {
            name: "Alice".into(),
            age: 30,
        };
        let msg = encode(&original).expect("encode");
        let got: Person = decode(&msg).expect("decode");
        assert_eq!(original, got);
        drop(msg);
    }
}
