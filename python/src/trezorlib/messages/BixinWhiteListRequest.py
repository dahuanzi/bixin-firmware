# Automatically generated by pb2py
# fmt: off
from .. import protobuf as p

if __debug__:
    try:
        from typing import Dict, List  # noqa: F401
        from typing_extensions import Literal  # noqa: F401
        EnumTypeWL_OperationType = Literal[0, 1, 2]
    except ImportError:
        pass


class BixinWhiteListRequest(p.MessageType):
    MESSAGE_WIRE_TYPE = 911

    def __init__(
        self,
        type: EnumTypeWL_OperationType = None,
        addr_in: str = None,
    ) -> None:
        self.type = type
        self.addr_in = addr_in

    @classmethod
    def get_fields(cls) -> Dict:
        return {
            1: ('type', p.EnumType("WL_OperationType", (0, 1, 2)), 0),  # required
            2: ('addr_in', p.UnicodeType, 0),
        }