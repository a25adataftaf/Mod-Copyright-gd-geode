"""Research pipeline exports."""

from backend.app.research.ai_providers import (
    AIResearchAdapter,
    AISuggestedAnalysis,
    ConfigurableHttpAIAdapter,
    DailyBudgetTracker,
)
from backend.app.research.collectors import CollectedEvidenceItem, EvidenceBundle, collect_evidence
from backend.app.research.evaluator import EvaluatedSongRights, evaluate_song_rights
from backend.app.research.identity import compute_canonical_key, detect_derivative_characteristics

__all__ = [
    "AIResearchAdapter",
    "AISuggestedAnalysis",
    "CollectedEvidenceItem",
    "ConfigurableHttpAIAdapter",
    "DailyBudgetTracker",
    "EvaluatedSongRights",
    "EvidenceBundle",
    "collect_evidence",
    "compute_canonical_key",
    "detect_derivative_characteristics",
    "evaluate_song_rights",
]
