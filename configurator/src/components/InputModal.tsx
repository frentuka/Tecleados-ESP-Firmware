import React, { useState, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import '../assets/css/input-modal.css';

interface InputModalProps {
    isOpen: boolean;
    title: string;
    initialValue?: string;
    placeholder?: string;
    maxLength?: number;
    onSubmit: (value: string) => void;
    onCancel: () => void;
}

export default function InputModal({ 
    isOpen, 
    title, 
    initialValue = '', 
    placeholder = '', 
    maxLength = 15,
    onSubmit, 
    onCancel 
}: InputModalProps) {
    const [value, setValue] = useState(initialValue);
    const inputRef = useRef<HTMLInputElement>(null);

    useEffect(() => {
        if (isOpen) {
            setValue(initialValue);
            setTimeout(() => inputRef.current?.focus(), 100);
        }
    }, [isOpen, initialValue]);

    if (!isOpen) return null;

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault();
        const trimmed = value.trim();
        if (trimmed) onSubmit(trimmed);
    };

    return createPortal(
        <div className="modal-overlay" onMouseDown={onCancel}>
            <div className="modal-content input-modal" onMouseDown={e => e.stopPropagation()} onClick={e => e.stopPropagation()}>
                <div className="input-modal-header">
                    <h3>{title}</h3>
                    <button type="button" className="btn-close-inline" onClick={onCancel} title="Close">
                        <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
                            <line x1="18" y1="6" x2="6" y2="18"></line>
                            <line x1="6" y1="6" x2="18" y2="18"></line>
                        </svg>
                    </button>
                </div>
                <form onSubmit={handleSubmit} className="input-modal-form">
                    <input
                        ref={inputRef}
                        type="text"
                        value={value}
                        onChange={e => setValue(e.target.value)}
                        placeholder={placeholder}
                        maxLength={maxLength}
                    />
                    <div className="input-modal-actions">
                        <button type="button" className="btn btn-secondary" onClick={onCancel}>Cancel</button>
                        <button type="submit" className="btn" disabled={!value.trim()}>Save</button>
                    </div>
                </form>
            </div>
        </div>,
        document.body
    );
}
