// SPDX-License-Identifier: GPL-3.0-or-later
//! Decode bounded ProtoJSON tuner state from the Shack Protocol read API.

mod bindings;

use serde::{Deserialize, Serialize};
use std::{error::Error, fmt};

pub use bindings::{MAX_PACKET, TUNER_STATE_PATH};
pub const TUNER_DEVICE_ID: &str = "tuner";

#[derive(Debug, PartialEq, Eq)]
pub enum ReadError {
    Length,
    InvalidJson,
    Version,
    Identity,
}

impl fmt::Display for ReadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Length => "invalid Shack response length",
            Self::InvalidJson => "invalid Shack tuner state",
            Self::Version => "unsupported Shack protocol version",
            Self::Identity => "wrong Shack request, node or device identity",
        })
    }
}

impl Error for ReadError {}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct Version {
    pub major: u32,
    #[serde(default)]
    pub minor: u32,
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq, Default)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum TunerMode {
    #[default]
    TunerModeUnknown,
    TunerModeManual,
    TunerModeAuto,
    TunerModeControlled,
    TunerModeBypass,
}

#[derive(Debug, Deserialize, Serialize, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct RfMeter {
    pub forward_watts: Option<f64>,
    pub reflected_watts: Option<f64>,
    pub swr: Option<f64>,
}

#[derive(Debug, Deserialize, Serialize, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct TunerState {
    pub device_id: String,
    #[serde(default)]
    pub connected: bool,
    #[serde(default)]
    pub busy: bool,
    #[serde(default)]
    pub sequence: u32,
    #[serde(default)]
    pub mode: TunerMode,
    pub antenna: Option<u32>,
    pub meter_age_ms: Option<u32>,
    pub meter: Option<RfMeter>,
}

#[derive(Debug, Deserialize, Serialize, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct TunerReply {
    pub version: Version,
    pub request_id: String,
    pub node_id: String,
    pub tuner_state: TunerState,
}

fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= bindings::MAX_ID
        && id
            .bytes()
            .all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, b'.' | b'_' | b'-'))
}

/// Decode the HTTP response. A gateway that generates and checks the
/// request ID on behalf of the caller can pass `None` for that ID.
pub fn decode_tuner_state(
    bytes: &[u8],
    device: &str,
    request: Option<&str>,
) -> Result<TunerReply, ReadError> {
    if bytes.is_empty() || bytes.len() > MAX_PACKET {
        return Err(ReadError::Length);
    }
    let reply: TunerReply = serde_json::from_slice(bytes).map_err(|_| ReadError::InvalidJson)?;
    if (reply.version.major, reply.version.minor) != (bindings::MAJOR, bindings::MINOR) {
        return Err(ReadError::Version);
    }
    if !valid_id(&reply.request_id)
        || !valid_id(&reply.node_id)
        || !valid_id(&reply.tuner_state.device_id)
        || reply.tuner_state.device_id != device
        || request.is_some_and(|id| id != reply.request_id)
    {
        return Err(ReadError::Identity);
    }
    Ok(reply)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bindings_match_the_shared_manifest() {
        let manifest: serde_json::Value =
            serde_json::from_str(include_str!("../../../protocol/bindings.json")).unwrap();
        assert_eq!(manifest["limits"]["message_bytes"], MAX_PACKET);
        assert_eq!(manifest["limits"]["id_bytes"], bindings::MAX_ID);
        assert_eq!(manifest["version"]["major"], bindings::MAJOR);
        assert_eq!(manifest["version"]["minor"], bindings::MINOR);
        let route = manifest["http"]["get_tuner_state"].as_str().unwrap();
        assert_eq!(
            route
                .strip_prefix("GET ")
                .unwrap()
                .replace("{major}", &bindings::MAJOR.to_string())
                .replace("{device_id}", TUNER_DEVICE_ID),
            TUNER_STATE_PATH
        );
    }

    #[test]
    fn protocol_vector_preserves_known_zero() {
        let vector: serde_json::Value = serde_json::from_str(include_str!(
            "../../../protocol/tests/tuner_state_vector.json"
        ))
        .unwrap();
        let bytes = serde_json::to_vec(&vector["response"]).unwrap();
        let reply = decode_tuner_state(&bytes, "tuner.hf", Some("req-1")).unwrap();
        assert_eq!(reply.node_id, "shack-a");
        assert_eq!(reply.tuner_state.mode, TunerMode::TunerModeAuto);
        assert_eq!(reply.tuner_state.antenna, Some(0));
        assert_eq!(reply.tuner_state.meter_age_ms, Some(0));
        assert_eq!(reply.tuner_state.meter.unwrap().forward_watts, Some(0.0));
        assert_eq!(
            decode_tuner_state(&bytes, "tuner.hf", Some("another")),
            Err(ReadError::Identity)
        );
    }

    #[test]
    fn accepts_absence_and_checks_version_identity_and_limit() {
        let mut json = serde_json::json!({
            "version": {"major": 1},
            "requestId": "usc-1",
            "nodeId": "30aea41ad930",
            "tunerState": {"deviceId": TUNER_DEVICE_ID, "sequence": 7}
        });
        let decode = |value: &serde_json::Value, device| {
            decode_tuner_state(&serde_json::to_vec(value).unwrap(), device, None)
        };
        let state = decode(&json, TUNER_DEVICE_ID).unwrap().tuner_state;
        assert!(!state.connected);
        assert!(state.antenna.is_none() && state.meter.is_none());
        assert_eq!(decode(&json, "wrong"), Err(ReadError::Identity));
        json["version"]["minor"] = 1.into();
        assert_eq!(decode(&json, TUNER_DEVICE_ID), Err(ReadError::Version));
        assert_eq!(
            decode_tuner_state(&[b' '; MAX_PACKET + 1], TUNER_DEVICE_ID, None),
            Err(ReadError::Length)
        );
    }
}
