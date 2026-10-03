from debug_gym.gym.entities import Event, Observation
from debug_gym.gym.tools.tool import EnvironmentTool
from debug_gym.gym.tools.toolbox import Toolbox
from typing import List, Dict, Optional, Any

@Toolbox.register()
class PlanTool(EnvironmentTool):
    name: str = "update_plan"
    description = (
        "Create or update the task plan which tracks steps and progress. "
        "Provide an explanation and a full list of plan items. "
        "The explanation should be a concise summary of the change made and highlight any important context or next step. "
        "Each plan item must have a 'step' (the task description) and a 'status' ('pending', 'in_progress', or 'completed'). "
        "At most one step should be 'in_progress' at any given time. "
        "This tool helps to demonstrate that you've understood the task and convey how you're approaching it. "
        "Plans can help to make complex, ambiguous, or multi-phase work clearer and more collaborative for the user. "
        "A good plan should break the task into meaningful, logically ordered steps that are easy to verify as you go. "
    )
    arguments = {
        "plan": {
            "type": ["array"],
            "description": "The full list of plan steps. Each item must be an object with 'step' and 'status'.",
            "items": {
                "type": "object",
                "properties": {
                    "step": {
                        "type": "string",
                        "description": "A description of the individual step."
                    },
                    "status": {
                        "type": "string",
                        "description": "One of: pending, in_progress, completed."
                    }
                },
                "required": ["step", "status"]
            }
        },
        "explanation": {
            "type": ["string"],
            "description": "An explanation for why the plan is being updated (e.g., 'Initial plan created.', 'Pivoting to a new approach.').",
        }
    }

    def use(self, environment, plan: List[Dict[str, Any]], explanation: Optional[str] = None) -> Observation:
        if not isinstance(plan, list):
            msg = "Validation Error: 'plan' must be a list."
            self.queue_event(environment=environment, event=Event.PLAN_UPDATE_FAIL, message=msg, plan=plan)
            return Observation(self.name, msg)

        in_progress_count = 0
        valid_statuses = {"pending", "in_progress", "completed"}

        for i, item in enumerate(plan):
            if not isinstance(item, dict):
                msg = f"Validation Error: Plan item {i} is not a dictionary."
                return Observation(self.name, msg)
            
            if "step" not in item or "status" not in item:
                msg = f"Validation Error: Plan item {i} is missing 'step' or 'status'."
                return Observation(self.name, msg)

            status = item.get("status")
            if status not in valid_statuses:
                msg = f"Validation Error: Plan item {i} has invalid status '{status}'. Must be one of {valid_statuses}."
                return Observation(self.name, msg)
            
            if status == "in_progress":
                in_progress_count += 1

        if in_progress_count > 1:
            msg = "Validation Error: At most one step can be 'in_progress' at a time."
            return Observation(self.name, msg)
        
        obs_output = "Plan updated."
        if explanation:
            obs_output = f"Plan updated: {explanation}"

        return Observation(self.name, obs_output)
