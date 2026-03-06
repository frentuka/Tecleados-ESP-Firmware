import React, { createContext, useContext, useState, useCallback } from 'react';
import type { ReactNode } from 'react';
import { createPortal } from 'react-dom';

interface ConfirmModalContextType {
    confirm: (title: string, description: string) => Promise<boolean>;
}

const ConfirmModalContext = createContext<ConfirmModalContextType | undefined>(undefined);

export const useConfirm = (): ConfirmModalContextType => {
    const context = useContext(ConfirmModalContext);
    if (!context) {
        throw new Error('useConfirm must be used within a ConfirmModalProvider');
    }
    return context;
};

export const ConfirmModalProvider: React.FC<{ children: ReactNode }> = ({ children }) => {
    const [isOpen, setIsOpen] = useState(false);
    const [title, setTitle] = useState('');
    const [description, setDescription] = useState('');
    const [resolver, setResolver] = useState<{ resolve: (value: boolean) => void } | null>(null);

    const confirm = useCallback((confirmTitle: string, confirmDescription: string): Promise<boolean> => {
        setTitle(confirmTitle);
        setDescription(confirmDescription);
        setIsOpen(true);
        return new Promise((resolve) => {
            setResolver({ resolve });
        });
    }, []);

    const handleConfirm = () => {
        if (resolver) resolver.resolve(true);
        setIsOpen(false);
    };

    const handleCancel = () => {
        if (resolver) resolver.resolve(false);
        setIsOpen(false);
    };

    return (
        <ConfirmModalContext.Provider value={{ confirm }}>
            {children}
            {isOpen && createPortal(
                <div className="modal-overlay" style={{ zIndex: 99999 }}>
                    <div className="macro-modal" style={{ maxWidth: '400px', padding: '24px' }}>
                        <div className="macro-modal-header" style={{ marginBottom: '16px', paddingBottom: '0', border: 'none' }}>
                            <h2 style={{ fontSize: '1.25rem', color: 'white', margin: 0 }}>{title}</h2>
                        </div>
                        <div className="macro-modal-content" style={{ color: '#aaaaaa', fontSize: '0.9rem', marginBottom: '24px', lineHeight: '1.5' }}>
                            {description}
                        </div>
                        <div className="macro-modal-actions" style={{ display: 'flex', justifyContent: 'flex-end', gap: '12px' }}>
                            <button className="btn btn-secondary" onClick={handleCancel}>
                                Cancel
                            </button>
                            <button className="btn btn-danger" style={{ backgroundColor: '#ff4444', color: 'white', border: 'none' }} onClick={handleConfirm}>
                                Confirm
                            </button>
                        </div>
                    </div>
                </div>,
                document.body
            )}
        </ConfirmModalContext.Provider>
    );
};
