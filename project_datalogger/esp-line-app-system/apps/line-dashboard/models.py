"""Wire formats shared with the ESP32 firmware."""
from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, HttpUrl, field_validator

UInt32 = Annotated[int, Field(ge=0, le=4294967295)]
UInt16 = Annotated[int, Field(ge=0, le=65535)]
Int16 = Annotated[int, Field(ge=-32768, le=32767)]
TargetID = Annotated[str, Field(pattern=r"^[UCR][0-9a-f]{32}$")]


class Telemetry(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_.:-]+$")
    uptime_ms: UInt32
    online: bool
    mode: int = Field(ge=0, le=4)
    temperature10: Int16
    humidity10: UInt16
    pressure10: UInt16
    lightLux: UInt32
    noise10: UInt16
    validMask: int = Field(ge=0, le=255)
    adc1Mv: UInt16
    adc2Mv: UInt16
    diMask: int = Field(ge=0, le=255)
    relays: int = Field(ge=0, le=255)
    thermalReady: bool
    thermalAmbient10: Int16
    thermalCenter10: Int16
    thermalMin10: Int16
    thermalMax10: Int16


def format_telemetry(data: Telemetry) -> str:
    def sensor(value: int, bit: int, unit: str, scale: int = 10) -> str:
        if not data.online or not data.validMask & bit:
            return "unavailable"
        return f"{value / scale:.1f} {unit}" if scale == 10 else f"{value} {unit}"

    lines = [
        f"Smart Datalogger: {data.device_id}",
        f"STM32: {'ONLINE' if data.online else 'OFFLINE'} | mode: {data.mode}",
        f"Uptime: {data.uptime_ms // 1000} s",
        f"Temperature: {sensor(data.temperature10, 1, 'C')}",
        f"Humidity: {sensor(data.humidity10, 1, '%')}",
        f"Pressure: {sensor(data.pressure10, 2, 'hPa')}",
        f"Light: {sensor(data.lightLux, 4, 'lux', 1)}",
        f"Noise: {sensor(data.noise10, 8, 'dB')}",
    ]
    if data.online:
        lines.append(f"ADC1/ADC2: {data.adc1Mv}/{data.adc2Mv} mV")
        lines.append(f"Digital inputs: 0x{data.diMask:02X} | relays: 0x{data.relays:02X}")
    else:
        lines.append("ADC / digital inputs / relays: unavailable")
    if data.online and data.thermalReady and data.mode == 2:
        lines.append(
            f"Thermal C: ambient {data.thermalAmbient10 / 10:.1f}, "
            f"center {data.thermalCenter10 / 10:.1f}, "
            f"min {data.thermalMin10 / 10:.1f}, max {data.thermalMax10 / 10:.1f}"
        )
    else:
        lines.append("Thermal: unavailable")
    return "\n".join(lines)


class ManualMessage(BaseModel):
    model_config = ConfigDict(extra="forbid")
    message: str = Field(min_length=1, max_length=5000)
    target: TargetID | None = None
    user_id: TargetID | None = None
    type: Literal["user", "group", "broadcast"] | None = None
    broadcast: bool = False
    image: HttpUrl | None = None
    sticker: list[str] | None = Field(default=None, min_length=2, max_length=2)

    @field_validator("message")
    @classmethod
    def validate_message(cls, value: str) -> str:
        if not value.strip():
            raise ValueError("Message must not be blank")
        if len(value.encode("utf-16-le")) // 2 > 5000:
            raise ValueError("Message exceeds 5000 UTF-16 units")
        return value

    @field_validator("image")
    @classmethod
    def https_image(cls, value: HttpUrl | None) -> HttpUrl | None:
        if value is not None and value.scheme != "https":
            raise ValueError("Image URL must use HTTPS")
        return value

    @field_validator("sticker")
    @classmethod
    def numeric_sticker(cls, value: list[str] | None) -> list[str] | None:
        if value is not None and not all(v.isascii() and v.isdigit() for v in value):
            raise ValueError("Sticker package and sticker ID must be numeric strings")
        return value

    def messages(self) -> list[dict]:
        result = [{"type": "text", "text": self.message}]
        if self.image:
            result.append({"type": "image", "originalContentUrl": str(self.image), "previewImageUrl": str(self.image)})
        if self.sticker:
            result.append({"type": "sticker", "packageId": self.sticker[0], "stickerId": self.sticker[1]})
        return result
