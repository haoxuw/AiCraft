// The four server-authoritative action types. v1 sim only honors MOVE;
// RELOCATE/CONVERT/INTERACT are accepted as placeholders so later phases
// (Python artifacts) can wire them up without a rename.

import type { Vec2 } from './vec2';

export enum ActionType {
  MOVE = 0,
  RELOCATE = 1,
  CONVERT = 2,
  INTERACT = 3
}

export interface MoveAction {
  type: ActionType.MOVE;
  monster_id: number;
  vel: Vec2;
}

export interface RelocateAction {
  type: ActionType.RELOCATE;
  monster_id: number;
  food_id: number;
}

export interface ConvertAction {
  type: ActionType.CONVERT;
  monster_id: number;
  kind: 'split' | 'grow';
  amount: number;
}

export interface InteractAction {
  type: ActionType.INTERACT;
  monster_id: number;
  target_id: number;
}

export type Action = MoveAction | RelocateAction | ConvertAction | InteractAction;
export type ActionProposal = Action;
